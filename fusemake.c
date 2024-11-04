/*
  Based on the pass-through example given by libfuse.
*/
// TODO: error handling on reply functions.
// TODO: refuse access to mount dir to avoid circular directory structure.

#define _GNU_SOURCE
#define FUSE_USE_VERSION 34

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fuse_lowlevel.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/param.h>
#include <sys/xattr.h>
#include <unistd.h>

#define DEPEND_ATTR "fusemake.depend"
#define LENGTH(ARRAY) (sizeof(ARRAY) / sizeof((ARRAY)[0]))
#define DOT_FUSEMAKE ".fusemake"

#define eprintf(...) fprintf(stderr, __VA_ARGS__)
#define DEBUG(...) eprintf(__VA_ARGS__)

#define PROCESS_MAX (1 << 16)
// Inode one the root, so shift ino to exclude zero and one.
#define INO_PROCESS(ino) (((ino) - 2) % PROCESS_MAX)
#define INO_ID(ino) (((ino) - 2) / PROCESS_MAX)
#define INO(process, id) ((id) * PROCESS_MAX + (process) + 2)
typedef struct Ino {
	int process;
	int id;
} Ino;
Ino fuse2ino(fuse_ino_t ino) {
	return (Ino) { .process = INO_PROCESS(ino), .id = INO_ID(ino) };
}
fuse_ino_t ino2fuse(Ino ino) { return INO(ino.process, ino.id); }

typedef struct Inode {
	// Canonical path in project root (allocated).
	char *path;
	// Reference to the file name portion of path.
	char *name;
	// References to other inodes in this directory (INO_IDs of files).
	// 0 to indicate absence (no hard links exist to the rood directory).
	int parent, next, prev, child;
	// 0 if closed.
	int fd;
	bool exists;
	bool opened;
} Inode;
typedef struct Inode_List {
	Inode *list;
	// Lookup is the count of active lookups from the kernel.
	// Presses shouldn't be replaced until this drops to zero.
	int length, capacity, generation, lookup;
	bool active;
} Inode_List;

Inode_List processes[PROCESS_MAX];
int processes_len;
char *builder_path;

void ino_ref(Ino ino, int count) { processes[ino.process].lookup += count; }
// Use these instead of the fuse versions to handle lookups.
int fm_reply_entry(Ino entry, fuse_req_t req, struct fuse_entry_param *e) {
	printf("fm_reply_entry\n");
	ino_ref(entry, 1);
	return fuse_reply_entry(req, e);
}
int fm_reply_create(
	Ino entry,
	fuse_req_t req,
	struct fuse_entry_param *e,
	struct fuse_file_info *fi
) {
	printf("fm_reply_create\n");
	ino_ref(entry, 1);
	return fuse_reply_create(req, e, fi);
}

Inode *inode(Ino ino) { return &processes[ino.process].list[ino.id]; }

Ino ino_parent(Ino child) {
	child.id = inode(child)->parent;
	return child;
}
char ROOT[] = ".";
Ino add_root(int process) {
	const int initial_cap = 16;
	processes_len = MAX(process + 1, processes_len);
	processes[process] = (Inode_List) {
		.list = malloc(sizeof(Inode) * initial_cap),
		.length = 1,
		.capacity = initial_cap,
		.active = true,
		.generation = processes[process].generation + 1,
	};
	Inode *out = &processes[process].list[0];
	*out = (Inode) {
		.path = ROOT,
		.name = ROOT,
	};
	return (Ino) { .id = 0, .process = process };
}

int count_active_processes() {
	int out = 0;
	for (int i = 0; i < processes_len; ++i) {
		out += processes[i].active;
	}
	return out;
}

Ino ino_add_child(Ino parent, const char *name) {
	Inode_List *proc = &processes[parent.process];
	int cap = proc->capacity;
	if (cap == proc->length) {
		cap += cap / 2;
		proc->list = reallocarray(proc->list, cap, sizeof(*proc->list));
		proc->capacity = cap;
	}

	int p_len = strlen(inode(parent)->path);
	int name_len = strlen(name);
	char *path = malloc(p_len + 1 + name_len + 1);
	sprintf(path, "%s/%s", inode(parent)->path, name);
	Ino out_ino = { .process = parent.process, .id = proc->length };
	Inode *out = &proc->list[proc->length++];
	*out = (Inode) {
		.name = &path[p_len + 1],
		.path = path,
		.parent = parent.id,
		.next = inode(parent)->child,
	};
	if (inode(parent)->child != 0) {
		proc->list[inode(parent)->child].prev = out_ino.id;
	}
	inode(parent)->child = out_ino.id;

	return out_ino;
}

Ino ino_get_child(Ino parent, const char *name, bool *found) {
	Inode_List *proc = &processes[parent.process];
	Inode *c;
	for (int child = inode(parent)->child; child != 0; child = c->next) {
		c = &proc->list[child];
		if (0 == strcmp(name, c->name)) {
			*found = true;
			return (Ino) { .id = child, .process = parent.process };
		}
	}
	*found = false;
	return (Ino) {};
}
Ino ino_child(Ino parent, const char *name) {
	bool found;
	Ino out = ino_get_child(parent, name, &found);
	if (!found)
		out = ino_add_child(parent, name);
	return out;
}

// TODO: remember to handle symlinked dependencies.
// TODO: remember to block for dependencies.

static void fm_init(void *userdata, struct fuse_conn_info *conn) {
	DEBUG("fm_init\n");
}

static void fm_destroy(void *userdata) {
	DEBUG("fm_destroy\n");
	// Not really worth freeing since program will just exit.
}

static void
fm_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
	struct stat buf = {};
	if (ino == 1) {
		DEBUG("fm_getattr root\n");
		buf.st_mode = S_IFDIR | 0755;
		buf.st_nlink = count_active_processes() + 2;
	} else {
		Inode *i = inode(fuse2ino(ino));
		DEBUG("fm_getattr %s\n", i->path);

		int res = lstat(i->path, &buf);
		if (res == -1)
			return (void)fuse_reply_err(req, errno);
	}

	fuse_reply_attr(req, &buf, 0);
}

static void fm_setattr(
	fuse_req_t req,
	fuse_ino_t ino,
	struct stat *attr,
	int to_set,
	struct fuse_file_info *fi
) {
	DEBUG("fm_setattr\n");
	if (ino == 1)
		return (void)fuse_reply_err(req, ENOTSUP);
	Inode *i = inode(fuse2ino(ino));
	int res;

	if (to_set & FUSE_SET_ATTR_MODE) {
		res = chmod(i->path, attr->st_mode);
		if (res == -1)
			goto out_err;
	}
	if (to_set & (FUSE_SET_ATTR_UID | FUSE_SET_ATTR_GID)) {
		uid_t uid = (to_set & FUSE_SET_ATTR_UID) ? attr->st_uid : (uid_t)-1;
		gid_t gid = (to_set & FUSE_SET_ATTR_GID) ? attr->st_gid : (gid_t)-1;

		res = lchown(i->path, uid, gid);
		if (res == -1)
			goto out_err;
	}
	if (to_set & FUSE_SET_ATTR_SIZE) {
		res = truncate(i->path, attr->st_size);
		if (res == -1)
			goto out_err;
	}
	if (to_set & (FUSE_SET_ATTR_ATIME | FUSE_SET_ATTR_MTIME)) {
		struct timespec tv[2];

		tv[0].tv_sec = 0;
		tv[1].tv_sec = 0;
		tv[0].tv_nsec = UTIME_OMIT;
		tv[1].tv_nsec = UTIME_OMIT;

		if (to_set & FUSE_SET_ATTR_ATIME_NOW)
			tv[0].tv_nsec = UTIME_NOW;
		else if (to_set & FUSE_SET_ATTR_ATIME)
			tv[0] = attr->st_atim;

		if (to_set & FUSE_SET_ATTR_MTIME_NOW)
			tv[1].tv_nsec = UTIME_NOW;
		else if (to_set & FUSE_SET_ATTR_MTIME)
			tv[1] = attr->st_mtim;

		res = utimensat(0, i->path, tv, AT_SYMLINK_NOFOLLOW);
		if (res == -1)
			goto out_err;
	}

	return fm_getattr(req, ino, fi);

out_err:
	fuse_reply_err(req, errno);
}

static void fill_entry_ino(Ino ino, struct fuse_entry_param *e) {
	e->ino = ino2fuse(ino);
	e->generation = processes[ino.process].generation;
}
static int fill_entry(Ino ino, struct fuse_entry_param *e) {
	int res;

	memset(e, 0, sizeof(*e));
	res = lstat(inode(ino)->path, &e->attr);
	DEBUG("fill_entry %s\n", inode(ino)->path);

	if (res == -1)
		goto out_err;

	inode(ino)->exists = true;
	fill_entry_ino(ino, e);

	return 0;

out_err:
	return errno;
}

static void fm_lookup(fuse_req_t req, fuse_ino_t parent, const char *name) {
	if (0 == strcmp(name, DOT_FUSEMAKE))
		return (void)fuse_reply_err(req, ENOENT);
	struct fuse_entry_param e;
	int err;
	Ino child;
	if (parent == 1) {
		DEBUG("fm_lookup root %s\n", name);
		if (*name == '\0')
			return (void)fuse_reply_err(req, ENOENT);
		int process = 0;
		for (; *name != '\0'; ++name) {
			if ('0' <= *name && *name <= '9') {
				process *= 10;
				process += *name - '0';
			} else {
				return (void)fuse_reply_err(req, ENOENT);
			}
		}
		if (processes[process].active)
			child = (Ino) { .id = 0, .process = process };
		else
			return (void)fuse_reply_err(req, ENOENT);
	} else {
		child = ino_child(fuse2ino(parent), name);
		DEBUG("fm_lookup %s\n", inode(child)->path);
	}
	err = fill_entry(child, &e);
	if (err)
		fuse_reply_err(req, err);
	else
		fm_reply_entry(child, req, &e);
}

static void fm_mknod_symlink(
	fuse_req_t req,
	fuse_ino_t parent,
	const char *name,
	mode_t mode,
	dev_t rdev,
	const char *link
) {
	if (0 == strcmp(name, DOT_FUSEMAKE))
		return (void)fuse_reply_err(req, ENOTSUP);
	DEBUG("fm_mknod_symlink\n");
	if (parent == 1)
		return (void)fuse_reply_err(req, ENOTSUP);
	int res;
	Ino child = ino_child(fuse2ino(parent), name);
	struct fuse_entry_param e;

	if (link == NULL) {
		res = mknod(inode(child)->path, mode, rdev);
	} else {
		res = symlink(inode(child)->path, link);
	}

	if (res == -1)
		goto out;

	errno = fill_entry(child, &e);
	if (errno)
		goto out;

	fm_reply_entry(child, req, &e);
	return;

out:
	fuse_reply_err(req, errno);
}

static void fm_mknod(
	fuse_req_t req, fuse_ino_t parent, const char *name, mode_t mode, dev_t rdev
) {
	DEBUG("fm_mknod\n");
	fm_mknod_symlink(req, parent, name, mode, rdev, NULL);
}

static void
fm_mkdir(fuse_req_t req, fuse_ino_t parent, const char *name, mode_t mode) {
	DEBUG("fm_mkdir\n");
	fm_mknod_symlink(req, parent, name, S_IFDIR | mode, 0, NULL);
}

static void fm_symlink(
	fuse_req_t req, const char *link, fuse_ino_t parent, const char *name
) {
	DEBUG("fm_symlink\n");
	fm_mknod_symlink(req, parent, name, S_IFLNK, 0, link);
}

static void
fm_link(fuse_req_t req, fuse_ino_t ino, fuse_ino_t parent, const char *name) {
	if (0 == strcmp(name, DOT_FUSEMAKE))
		return (void)fuse_reply_err(req, ENOTSUP);
	DEBUG("fm_link\n");
	if (ino == 1 || parent == 1)
		return (void)fuse_reply_err(req, ENOTSUP);
	int res;
	struct fuse_entry_param e;
	Ino from = fuse2ino(ino);
	Ino to = ino_child(fuse2ino(parent), name);

	memset(&e, 0, sizeof(struct fuse_entry_param));

	res = link(inode(from)->path, inode(to)->path);
	if (res == -1)
		goto out_err;

	res = fill_entry(to, &e);
	if (res == -1)
		goto out_err;

	fm_reply_entry(to, req, &e);
	return;

out_err:
	fuse_reply_err(req, errno);
}

static void fm_rmdir(fuse_req_t req, fuse_ino_t parent, const char *name) {
	if (0 == strcmp(name, DOT_FUSEMAKE))
		return (void)fuse_reply_err(req, ENOTSUP);
	DEBUG("fm_rmdir\n");
	int res;
	if (parent == 1)
		return (void)fuse_reply_err(req, EPERM);
	Ino ino = ino_child(fuse2ino(parent), name);

	res = rmdir(inode(ino)->path);

	fuse_reply_err(req, res == -1 ? errno : 0);
}

static void fm_rename(
	fuse_req_t req,
	fuse_ino_t parent,
	const char *name,
	fuse_ino_t newparent,
	const char *newname,
	unsigned int flags
) {
	if (0 == strcmp(name, DOT_FUSEMAKE) || 0 == strcmp(newname, DOT_FUSEMAKE))
		return (void)fuse_reply_err(req, ENOTSUP);
	DEBUG("fm_rename\n");
	int res;
	if (parent == 1 || newparent == 1)
		return (void)fuse_reply_err(req, EPERM);
	// Not sure if this is entirely correct, but it's what the example does.
	if (flags)
		return (void)fuse_reply_err(req, EINVAL);
	Ino old = ino_child(fuse2ino(parent), name);
	Ino new = ino_child(fuse2ino(newparent), newname);

	res = rename(inode(old)->path, inode(new)->path);

	fuse_reply_err(req, res == -1 ? errno : 0);
}

static void fm_unlink(fuse_req_t req, fuse_ino_t parent, const char *name) {
	if (0 == strcmp(name, DOT_FUSEMAKE))
		return (void)fuse_reply_err(req, ENOTSUP);
	DEBUG("fm_unlink\n");
	int res;
	if (parent == 1)
		return (void)fuse_reply_err(req, EPERM);
	res = unlink(inode(ino_child(fuse2ino(parent), name))->path);

	fuse_reply_err(req, res == -1 ? errno : 0);
}

static void fm_forget_n(fuse_ino_t ino, uint64_t nlookup) {
	DEBUG("fm_forget_n %d %s\n", (int)nlookup, inode(fuse2ino(ino))->path);
	if (ino != 1)
		ino_ref(fuse2ino(ino), -nlookup);
}
static void fm_forget(fuse_req_t req, fuse_ino_t ino, uint64_t nlookup) {
	DEBUG("fm_forget\n");
	fm_forget_n(ino, nlookup);
	fuse_reply_none(req);
}
static void fm_forget_multi(
	fuse_req_t req, size_t count, struct fuse_forget_data *forgets
) {
	DEBUG("fm_forget_multi\n");
	for (int i = 0; i < count; i++)
		fm_forget_n(forgets[i].ino, forgets[i].nlookup);
	fuse_reply_none(req);
}

// Is this guaranteed to be a symlink? Don't need to check if so.
static void fm_readlink(fuse_req_t req, fuse_ino_t ino) {
	DEBUG("fm_readlink\n");
	char buf[PATH_MAX + 1];
	int res;
	if (ino == 1)
		return (void)fuse_reply_err(req, EINVAL);

	res = readlink(inode(fuse2ino(ino))->path, buf, sizeof(buf));
	if (res == -1)
		return (void)fuse_reply_err(req, errno);

	if (res == sizeof(buf))
		return (void)fuse_reply_err(req, ENAMETOOLONG);

	buf[res] = '\0';

	fuse_reply_readlink(req, buf);
}

static void
fm_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
	DIR *fd;
	if (ino != 1) {
		Inode *i = inode(fuse2ino(ino));
		DEBUG("fm_opendir %s\n", i->path);
		fd = opendir(i->path);
		if (fd == NULL)
			return (void)fuse_reply_err(req, errno);
		i->opened = true;
		fi->fh = (typeof(fi->fh))fd;
	} else {
		DEBUG("fm_opendir root\n");
	}
	fuse_reply_open(req, fi);
}

static int is_ignored_entry(const char *name) {
	return (name[0] == '.'
	        && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
	       || 0 == strcmp(name, DOT_FUSEMAKE);
}
static int fm_read_root(
	fuse_req_t req,
	fuse_ino_t ino,
	size_t size,
	off_t offset,
	struct fuse_file_info *fi,
	bool plus,
	char *buf,
	size_t *fill
) {
	DEBUG("fm_read_root\n");
	struct fuse_entry_param e;
	struct stat stbuf;
	if (plus) {
		for (int i = 0; i < processes_len; ++i) {
			if (processes[i].active) {
				fill_entry((Ino) { .id = 0, .process = i }, &e);
				break;
			}
		}
	} else {
		if (-1 == lstat(ROOT, &stbuf))
			return errno;
	}
	size_t ent_size;
	for (int i = offset; i < processes_len; ++i) {
		if (!processes[i].active)
			continue;
		Ino ino = { .id = 0, .process = i };
		char name[32];
		snprintf(name, LENGTH(name), "%d", i);
		if (plus) {
			fill_entry_ino(ino, &e);
			ino_ref(ino, 1);
			ent_size = fuse_add_direntry_plus(
				req, buf + *fill, size, name, &e, i + 1
			);
		} else {
			stbuf.st_ino = ino2fuse(ino);
			ent_size = fuse_add_direntry(
				req, buf + *fill, size, name, &stbuf, i + 1
			);
		}
		*fill += ent_size;
		if (*fill > size) {
			*fill -= ent_size;
			break;
		}
	}
	return 0;
}

static void fm_do_readdir(
	fuse_req_t req,
	fuse_ino_t ino,
	size_t size,
	off_t offset,
	struct fuse_file_info *fi,
	bool plus
) {
	DEBUG("fm_do_readdir\n");
	DIR *dir = (DIR *)fi->fh;
	char *buf;
	Ino parent = fuse2ino(ino);

	buf = malloc(size);
	if (!buf) {
		errno = ENOMEM;
		goto end;
	}
	size_t fill = 0;
	if (ino == 1) {
		errno = fm_read_root(req, ino, size, offset, fi, plus, buf, &fill);
		goto end;
	}

	seekdir(dir, offset);
	for (;;) {
		size_t ent_size;
		struct dirent *ent = readdir(dir);
		if (ent == NULL)
			goto end;
		// Skip these, the driver will return them for us.
		if (is_ignored_entry(ent->d_name))
			continue;
		Ino child = ino_child(parent, ent->d_name);
		if (plus) {
			struct fuse_entry_param e;
			errno = fill_entry(child, &e);
			if (errno)
				goto end;
			ino_ref(child, 1);
			ent_size = fuse_add_direntry_plus(
				req, buf + fill, size, ent->d_name, &e, telldir(dir)
			);
		} else {
			struct stat stbuf
				= { .st_ino = ino2fuse(child), .st_mode = ent->d_type << 12 };
			ent_size = fuse_add_direntry(
				req, buf + fill, size, ent->d_name, &stbuf, telldir(dir)
			);
		}

		fill += ent_size;
		if (fill > size) {
			fill -= ent_size;
			break;
		}
	}

	errno = 0;
end:
	// If there's an error, we can only signal it if we haven't stored
	// any entries yet - otherwise we'd end up with wrong lookup
	// counts for the entries that are already in the buffer. So we
	// return what we've collected until that point.
	if (errno && fill == 0)
		fuse_reply_err(req, errno);
	else
		fuse_reply_buf(req, buf, fill);
	free(buf);
}

static void fm_readdir(
	fuse_req_t req,
	fuse_ino_t ino,
	size_t size,
	off_t offset,
	struct fuse_file_info *fi
) {
	DEBUG("fm_readdir\n");
	fm_do_readdir(req, ino, size, offset, fi, false);
}

static void fm_readdirplus(
	fuse_req_t req,
	fuse_ino_t ino,
	size_t size,
	off_t offset,
	struct fuse_file_info *fi
) {
	DEBUG("fm_readdirplus\n");
	fm_do_readdir(req, ino, size, offset, fi, true);
}

static void
fm_releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
	DEBUG("fm_releasedir\n");
	if (ino != 1)
		closedir((DIR *)fi->fh);
	fuse_reply_err(req, 0);
}

static int fm_do_open(Ino ino, mode_t mode, struct fuse_file_info *fi) {
	DEBUG("fm_do_open %s\n", inode(ino)->name);
	int fd = open(inode(ino)->path, fi->flags & ~O_NOFOLLOW, mode);
	if (fd == -1)
		return errno;
	inode(ino)->opened = true;
	fi->fh = fd;
	fi->direct_io = 1;
	return 0;
}

static void fm_create(
	fuse_req_t req,
	fuse_ino_t parent,
	const char *name,
	mode_t mode,
	struct fuse_file_info *fi
) {
	if (0 == strcmp(name, DOT_FUSEMAKE))
		return (void)fuse_reply_err(req, ENOTSUP);
	DEBUG("fm_create\n");
	if (parent == 1)
		return (void)fuse_reply_err(req, EEXIST);
	Ino child = ino_child(fuse2ino(parent), name);
	struct fuse_entry_param e;
	int err;

	fi->flags |= O_CREAT;
	err = fm_do_open(child, mode, fi);
	if (err != 0)
		return (void)fuse_reply_err(req, err);
	err = fill_entry(child, &e);
	if (err)
		fuse_reply_err(req, err);
	else
		fm_reply_create(child, req, &e, fi);
}

static void fm_fsyncdir(
	fuse_req_t req, fuse_ino_t ino, int datasync, struct fuse_file_info *fi
) {
	DEBUG("fm_fsyncdir\n");
	int res;
	if (ino == 1)
		return (void)fuse_reply_err(req, 0);
	int fd = dirfd((DIR *)fi->fh);
	if (datasync)
		res = fdatasync(fd);
	else
		res = fsync(fd);
	fuse_reply_err(req, res == -1 ? errno : 0);
}

static void fm_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
	DEBUG("fm_open\n");
	if (ino == 1)
		return (void)fuse_reply_err(req, EISDIR);
	int err = fm_do_open(fuse2ino(ino), 0, fi);
	if (err == 0)
		fuse_reply_open(req, fi);
	else
		fuse_reply_err(req, err);
}

static void
fm_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
	DEBUG("fm_release %s\n", inode(fuse2ino(ino))->path);
	close(fi->fh);
	fuse_reply_err(req, 0);
}

static void
fm_flush(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
	DEBUG("fm_flush\n");
	int res;
	(void)ino;
	res = close(dup(fi->fh));
	fuse_reply_err(req, res == -1 ? errno : 0);
}

static void fm_fsync(
	fuse_req_t req, fuse_ino_t ino, int datasync, struct fuse_file_info *fi
) {
	DEBUG("fm_fsync\n");
	int res;
	(void)ino;
	if (datasync)
		res = fdatasync(fi->fh);
	else
		res = fsync(fi->fh);
	fuse_reply_err(req, res == -1 ? errno : 0);
}

static void fm_read(
	fuse_req_t req,
	fuse_ino_t ino,
	size_t size,
	off_t offset,
	struct fuse_file_info *fi
) {
	DEBUG("fm_read\n");
	struct fuse_bufvec buf = FUSE_BUFVEC_INIT(size);

	buf.buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
	buf.buf[0].fd = fi->fh;
	buf.buf[0].pos = offset;

	fuse_reply_data(req, &buf, FUSE_BUF_SPLICE_MOVE);
}

static void fm_write_buf(
	fuse_req_t req,
	fuse_ino_t ino,
	struct fuse_bufvec *in_buf,
	off_t off,
	struct fuse_file_info *fi
) {
	DEBUG("fm_write_buf\n");
	(void)ino;
	ssize_t res;
	struct fuse_bufvec out_buf = FUSE_BUFVEC_INIT(fuse_buf_size(in_buf));

	out_buf.buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
	out_buf.buf[0].fd = fi->fh;
	out_buf.buf[0].pos = off;

	res = fuse_buf_copy(&out_buf, in_buf, 0);
	if (res < 0)
		fuse_reply_err(req, -res);
	else
		fuse_reply_write(req, (size_t)res);
}

static void fm_statfs(fuse_req_t req, fuse_ino_t ino) {
	DEBUG("fm_statfs\n");
	int res;
	struct statvfs stbuf;

	if (ino != 1)
		res = statvfs(inode(fuse2ino(ino))->path, &stbuf);
	else
		res = statvfs(ROOT, &stbuf);
	if (res == -1)
		fuse_reply_err(req, errno);
	else
		fuse_reply_statfs(req, &stbuf);
}

static void fm_fallocate(
	fuse_req_t req,
	fuse_ino_t ino,
	int mode,
	off_t offset,
	off_t length,
	struct fuse_file_info *fi
) {
	DEBUG("fm_fallocate\n");
	int err = ENOTSUP;
	if (ino == 1)
		return (void)fuse_reply_err(req, err);

#ifdef HAVE_FALLOCATE
	err = fallocate(fi->fh, mode, offset, length);
	if (err < 0)
		err = errno;

#elif defined(HAVE_POSIX_FALLOCATE)
	if (mode)
		return (void)fuse_reply_err(req, ENOTSUP);
	err = posix_fallocate(fi->fh, offset, length);
#endif

	fuse_reply_err(req, err);
}

static void
fm_flock(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi, int op) {
	DEBUG("fm_flock\n");
	int res;
	if (ino == 1)
		return (void)fuse_reply_err(req, ENOTSUP);

	res = flock(fi->fh, op);

	fuse_reply_err(req, res == -1 ? errno : 0);
}

static void
fm_getxattr(fuse_req_t req, fuse_ino_t ino, const char *name, size_t size) {
	DEBUG("fm_getxattr\n");
	char *value = NULL;
	ssize_t ret;
	int saverr;
	if (ino == 1)
		return (void)fuse_reply_err(req, ENOTSUP);

	saverr = ENOSYS;

	if (size) {
		value = malloc(size);
		if (!value)
			goto out_err;

		ret = getxattr(inode(fuse2ino(ino))->path, name, value, size);
		if (ret == -1)
			goto out_err;
		saverr = 0;
		if (ret == 0)
			goto out;

		fuse_reply_buf(req, value, ret);
	} else {
		ret = getxattr(inode(fuse2ino(ino))->path, name, NULL, 0);
		if (ret == -1)
			goto out_err;

		fuse_reply_xattr(req, ret);
	}
out_free:
	free(value);
	return;

out_err:
	saverr = errno;
out:
	fuse_reply_err(req, saverr);
	goto out_free;
}

static void fm_listxattr(fuse_req_t req, fuse_ino_t ino, size_t size) {
	DEBUG("fm_listxattr\n");
	char *value = NULL;
	ssize_t ret;
	int saverr;
	if (ino == 1)
		return (void)fuse_reply_err(req, ENOTSUP);

	saverr = ENOSYS;

	if (size) {
		value = malloc(size);
		if (!value)
			goto out_err;

		ret = listxattr(inode(fuse2ino(ino))->path, value, size);
		if (ret == -1)
			goto out_err;
		saverr = 0;
		if (ret == 0)
			goto out;

		fuse_reply_buf(req, value, ret);
	} else {
		ret = listxattr(inode(fuse2ino(ino))->path, NULL, 0);
		if (ret == -1)
			goto out_err;

		fuse_reply_xattr(req, ret);
	}
out_free:
	free(value);
	return;

out_err:
	saverr = errno;
out:
	fuse_reply_err(req, saverr);
	goto out_free;
}

static void fm_setxattr(
	fuse_req_t req,
	fuse_ino_t ino,
	const char *name,
	const char *value,
	size_t size,
	int flags
) {
	DEBUG("fm_setxattr\n");
	ssize_t ret;
	if (ino == 1)
		return (void)fuse_reply_err(req, ENOTSUP);
	// REMEMBER: ino will be the directory and name the dependent file.
	if (0 != strcmp(name, DEPEND_ATTR))
		return (void)fuse_reply_err(req, 0);
	ret = setxattr(inode(fuse2ino(ino))->path, name, value, size, flags);
	fuse_reply_err(req, ret == -1 ? errno : 0);
}

static void fm_removexattr(fuse_req_t req, fuse_ino_t ino, const char *name) {
	DEBUG("fm_removexattr\n");
	ssize_t ret;
	if (ino == 1)
		return (void)fuse_reply_err(req, ENOTSUP);
	ret = removexattr(inode(fuse2ino(ino))->path, name);
	fuse_reply_err(req, ret == -1 ? errno : 0);
}

#ifdef HAVE_COPY_FILE_RANGE
static void fm_copy_file_range(
	fuse_req_t req,
	fuse_ino_t ino_in,
	off_t off_in,
	struct fuse_file_info *fi_in,
	fuse_ino_t ino_out,
	off_t off_out,
	struct fuse_file_info *fi_out,
	size_t len,
	int flags
) {
	DEBUG("fm_copy_file_range\n");
	ssize_t res;

	if (fm_debug(req))
		fuse_log(
			FUSE_LOG_DEBUG,
			"fm_copy_file_range(ino=%" PRIu64 "/fd=%lu, "
			"off=%lu, ino=%" PRIu64 "/fd=%lu, "
			"off=%lu, size=%zd, flags=0x%x)\n",
			ino_in,
			fi_in->fh,
			off_in,
			ino_out,
			fi_out->fh,
			off_out,
			len,
			flags
		);

	res = copy_file_range(fi_in->fh, &off_in, fi_out->fh, &off_out, len, flags);
	if (res < 0)
		fuse_reply_err(req, errno);
	else
		fuse_reply_write(req, res);
}
#endif

static void fm_lseek(
	fuse_req_t req,
	fuse_ino_t ino,
	off_t off,
	int whence,
	struct fuse_file_info *fi
) {
	DEBUG("fm_lseek\n");
	off_t res;

	(void)ino;
	res = lseek(fi->fh, off, whence);
	if (res != -1)
		fuse_reply_lseek(req, res);
	else
		fuse_reply_err(req, errno);
}

static const struct fuse_lowlevel_ops fm_oper = {
	.init = fm_init,
	.destroy = fm_destroy,
	.lookup = fm_lookup,
	.mkdir = fm_mkdir,
	.mknod = fm_mknod,
	.symlink = fm_symlink,
	.link = fm_link,
	.unlink = fm_unlink,
	.rmdir = fm_rmdir,
	.rename = fm_rename,
	.forget = fm_forget,
	.forget_multi = fm_forget_multi,
	.getattr = fm_getattr,
	.setattr = fm_setattr,
	.readlink = fm_readlink,
	.opendir = fm_opendir,
	.readdir = fm_readdir,
	.readdirplus = fm_readdirplus,
	.releasedir = fm_releasedir,
	.fsyncdir = fm_fsyncdir,
	.create = fm_create,
	.open = fm_open,
	.release = fm_release,
	.flush = fm_flush,
	.fsync = fm_fsync,
	.read = fm_read,
	.write_buf = fm_write_buf,
	.statfs = fm_statfs,
	.fallocate = fm_fallocate,
	.flock = fm_flock,
	.getxattr = fm_getxattr,
	.listxattr = fm_listxattr,
	.setxattr = fm_setxattr,
	.removexattr = fm_removexattr,
#ifdef HAVE_COPY_FILE_RANGE
	.copy_file_range = fm_copy_file_range,
#endif
	.lseek = fm_lseek,
};

#define MOUNT_POINT DOT_FUSEMAKE "/mount"
#define BUILDER DOT_FUSEMAKE "/builder"
static char *fuse_args[] = { "fusemake" };
void validate_executable(const char *builder) {
	int ret;
	if (access(builder, X_OK) != 0) {
		ret = errno;
		eprintf("Builder %s not found or not executable.\n", builder);
		exit(ret);
	}
}
#define IO_ERROR "IO error.\n"
#define CHECK_IO(op, ...) \
	{ \
		if (!(op)) { \
			int ret = errno; \
			eprintf(__VA_ARGS__); \
			exit(ret); \
		} \
	}
void chdir_to_root() {
	static char path[PATH_MAX];
	CHECK_IO(NULL != getcwd(path, LENGTH(path)), IO_ERROR);
	int depth = 1;
	// Ignore trailing /
	for (int i = 0; path[i] != '\0' && path[i + 1] != '\0'; ++i)
		depth += path[i] == '/';
	for (; depth >= 0; --depth) {
		if (0 == access(DOT_FUSEMAKE, R_OK | W_OK | X_OK))
			return;
		else if (errno != ENOENT)
			CHECK_IO(false, IO_ERROR);
	}
	eprintf("Could not find .fusemake directory.\nHave you initialised your "
	        "project with fusemake --init <builder>?\n");
	exit(ENOENT);
}
void depend(char *path) {
	static char dot[] = ".";
	char *dir = path, *name = strrchr(path, '/');
	if (path[0] == '\0') {
		eprintf("Error, empty argument.\n");
		return;
	}
	if (name == NULL) {
		dir = dot;
		name = path;
	} else {
		*(name++) = '\0';
	}
	CHECK_IO(
		0 == setxattr(dir, DEPEND_ATTR, name, strlen(name), 0),
		"Could not access file %s/%s.\n",
		dir,
		name
	);
	if (dir != dot)
		*(--name) = '/';
	printf("%s\n", path);
}
int main(int argc, char *argv[]) {
#define CHECK_3_ARGS(OP) \
	if (argc != 3) { \
		fprintf( \
			stderr, \
			"Too many arguments to fusemake " OP ".\n" \
			"\tUsage: fusemake " OP " /path/to/builder\n" \
		); \
		return E2BIG; \
	}
	int ret = -1;
	if (argc > 1 && 0 == strcmp(argv[1], "--help")) {
		char *arguments[][2] = {
			{ "--init <builder>",
			  "Initialise the current directory for building with builder." },
			{ "--set-builder <builder>", "Set the builder." },
			{ "--dpepend <args>",
			  "Build each of args asynchronously and write them to stdout." },
			{ "--depend", "Without arguments, use lines from stdin." },
		};
		printf("Usage: fusemake <targes>\n"
		       "Build the listed targets incrementally.\n"
		       "\n");
		int width = 0;
		for (int i = 0; i < LENGTH(arguments); ++i)
			width = MAX(width, strlen(arguments[i][0]));
		for (int i = 0; i < LENGTH(arguments); ++i)
			printf("\t%-*s%s\n", width + 4, arguments[i][0], arguments[i][1]);
	} else if (argc > 1 && 0 == strcmp(argv[1], "--init")) {
		CHECK_3_ARGS("--init");
		validate_executable(BUILDER);
#define MKDIR(DIR) \
	if (-1 == mkdir(DIR, 0755)) { \
		ret = errno; \
		if (errno == EEXIST) \
			fprintf( \
				stderr, \
				"Fusemake already initialised in this directory.\nDid you " \
				"mean --set-builder?\n" \
			); \
		else \
			fprintf( \
				stderr, \
				"Error creating directory %s, please remove .fusemake and " \
				"try again.\n", \
				DIR \
			); \
		return ret; \
	}
		MKDIR("/" DOT_FUSEMAKE);
		MKDIR("/" MOUNT_POINT);
		if (-1 == symlink(argv[2], BUILDER)) {
			ret = errno;
			fprintf(
				stderr,
				"Error creating symlink %s, please remove .fusermake and try "
				"again.\n",
				BUILDER
			);
			return ret;
		}
	} else if (argc > 1 && 0 == strcmp(argv[1], "--set-builder")) {
		char builder[PATH_MAX];
		CHECK_3_ARGS("--set-builder")
		validate_executable(argv[2]);
		CHECK_IO(NULL != realpath(argv[2], builder), IO_ERROR);
		chdir_to_root();
		CHECK_IO(
			0 == symlink(builder, BUILDER),
			"IO error, failed to symlink builder.\n"
		);
	} else if (argc > 1 && 0 == strcmp(argv[1], "--depend")) {
		if (argc >= 3) {
			for (int i = 2; i < argc; ++i)
				depend(argv[i]);
		} else {
			char *line = NULL;
			size_t line_len;
			for (;;) {
				errno = 0;
				if (0 == getline(&line, &line_len, stdin)) {
					CHECK_IO(errno == 0, IO_ERROR);
					break;
				}
				depend(line);
			}
			free(line);
		}
	} else {
		chdir_to_root();
		validate_executable(BUILDER);
		struct fuse_args args = FUSE_ARGS_INIT(LENGTH(fuse_args), fuse_args);
		struct fuse_session *se;

		/* Don't mask creation mode, kernel already did that */
		umask(0);
		add_root(0);

		se = fuse_session_new(&args, &fm_oper, sizeof(fm_oper), NULL);
		if (se == NULL)
			goto err_out1;

		if (fuse_set_signal_handlers(se) != 0)
			goto err_out2;

		if (fuse_session_mount(se, MOUNT_POINT) != 0)
			goto err_out3;

		fuse_daemonize(1);

		ret = fuse_session_loop(se);

		fuse_session_unmount(se);
	err_out3:
		fuse_remove_signal_handlers(se);
	err_out2:
		fuse_session_destroy(se);
	err_out1:
		fuse_opt_free_args(&args);

		return ret ? 1 : 0;
	}
}
