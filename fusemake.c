/*
  Based on the pass-through example given by libfuse.
*/

#include <time.h>
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
#include <sys/xattr.h>
#include <unistd.h>

#define LENGTH(ARRAY) (sizeof(ARRAY) / sizeof((ARRAY)[0]))

#define PROCESS_MAX (1 << 16)
// Inode one the root, so shift ino to exclude zero and one.
#define INO_PROCESS(ino) (((ino) - 2) % PROCESS_MAX)
#define INO_ID(ino) (((ino) - 2) / PROCESS_MAX)
#define INO(process, id) ((id) * PROCESS_MAX + (process) + 2)
typedef struct Ino {
	int process;
	int id;
} Ino;
const Ino NULL_INO = { .process = -1 };
bool null_ino(Ino ino) { return ino.process == -1; }
bool root_ino(Ino ino) { return ino.id == 0; }
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
} Inode_List;

Inode_List processes[PROCESS_MAX];
char project_root[PATH_MAX], *project_root_name;

void ino_ref(Ino ino, int count) { processes[ino.process].lookup += count; }
// Use these instead of the fuse versions to handle lookups.
int fm_reply_entry(Ino entry, fuse_req_t req, struct fuse_entry_param *e) {
	ino_ref(entry, 1);
	return fuse_reply_entry(req, e);
}
int fm_reply_create(
	Ino entry,
	fuse_req_t req,
	struct fuse_entry_param *e,
	struct fuse_file_info *fi
) {
	ino_ref(entry, 1);
	return fuse_reply_create(req, e, fi);
}

Inode *inode(Ino ino) { return &processes[ino.process].list[ino.id]; }

Ino ino_parent(Ino child) {
	child.id = inode(child)->parent;
	return child;
}

Ino add_root(int process) {
	const int initial_cap = 16;
	processes[process] = (Inode_List) {
		.list = malloc(sizeof(Inode) * initial_cap),
		.length = 1,
		.capacity = initial_cap,
	};
	Inode *out = &processes[process].list[0];
	*out = (Inode) {
		.path = project_root,
		.name = project_root_name,
	};
	return (Ino) { .id = 0, .process = process };
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

Ino ino_get_child(Ino parent, const char *name) {
	Inode_List *proc = &processes[parent.process];
	Inode *c;
	for (int child = inode(parent)->child; child != 0; child = c->next) {
		c = &proc->list[child];
		if (0 == strcmp(name, c->name))
			return (Ino) { .id = child, .process = parent.process };
	}
	return NULL_INO;
}
Ino ino_child(Ino parent, const char *name) {
	Ino out = ino_get_child(parent, name);
	if (null_ino(out))
		out = ino_add_child(parent, name);
	return out;
}

// TODO: remember to handle symlinked dependencies.
// TODO: remember to block for dependencies.

static void fm_init(void *userdata, struct fuse_conn_info *conn) {}

static void fm_destroy(void *userdata) {
	// Not really worth freeing since program will just exit.
}

static void
fm_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
	struct stat buf;
	Inode *i = inode(fuse2ino(ino));

	int res = lstat(i->path, &buf);
	if (res == -1) {
		fuse_reply_err(req, errno);
		return;
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

	if (res == -1)
		goto out_err;

	inode(ino)->exists = true;
	fill_entry_ino(ino, e);

	return 0;

out_err:
	return errno;
}

static void fm_lookup(fuse_req_t req, fuse_ino_t parent, const char *name) {
	struct fuse_entry_param e;
	int err;
	Ino child = ino_child(fuse2ino(parent), name);
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
	fm_mknod_symlink(req, parent, name, mode, rdev, NULL);
}

static void
fm_mkdir(fuse_req_t req, fuse_ino_t parent, const char *name, mode_t mode) {
	fm_mknod_symlink(req, parent, name, S_IFDIR | mode, 0, NULL);
}

static void fm_symlink(
	fuse_req_t req, const char *link, fuse_ino_t parent, const char *name
) {
	fm_mknod_symlink(req, parent, name, S_IFLNK, 0, link);
}

static void
fm_link(fuse_req_t req, fuse_ino_t ino, fuse_ino_t parent, const char *name) {
	int res, saverr;
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
	int res;
	if (parent == 1) {
		fuse_reply_err(req, EPERM);
		return;
	}
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
	int res;
	if (parent == 1 || newparent == 1) {
		fuse_reply_err(req, EPERM);
		return;
	}

	// Not sure if this is entirely correct, but it's what the example does.
	if (flags) {
		fuse_reply_err(req, EINVAL);
		return;
	}

	Ino old = ino_child(fuse2ino(parent), name);
	Ino new = ino_child(fuse2ino(newparent), newname);

	res = rename(inode(old)->path, inode(new)->path);

	fuse_reply_err(req, res == -1 ? errno : 0);
}

static void fm_unlink(fuse_req_t req, fuse_ino_t parent, const char *name) {
	int res;
	if (parent == 1) {
		fuse_reply_err(req, EPERM);
		return;
	}

	res = unlink(inode(ino_child(fuse2ino(parent), name))->path);

	fuse_reply_err(req, res == -1 ? errno : 0);
}

static void fm_forget_one(fuse_ino_t ino, uint64_t nlookup) {
	ino_ref(fuse2ino(ino), -nlookup);
}
static void fm_forget(fuse_req_t req, fuse_ino_t ino, uint64_t nlookup) {
	fm_forget_one(ino, nlookup);
	fuse_reply_none(req);
}
static void fm_forget_multi(
	fuse_req_t req, size_t count, struct fuse_forget_data *forgets
) {
	for (int i = 0; i < count; i++)
		fm_forget_one(forgets[i].ino, forgets[i].nlookup);
	fuse_reply_none(req);
}

static void fm_readlink(fuse_req_t req, fuse_ino_t ino) {
	char buf[PATH_MAX + 1];
	int res;

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
	int error = ENOMEM;
	DIR *fd;

	fd = opendir(inode(fuse2ino(ino))->path);
	inode(fuse2ino(ino))->opened = true;
	if (fd == NULL) {
		fuse_reply_err(req, errno);
		return;
	}

	fi->fh = (typeof(fi->fh))fd;
	fuse_reply_open(req, fi);
}

static int is_dot_or_dotdot(const char *name) {
	return name[0] == '.'
	       && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

static void fm_do_readdir(
	fuse_req_t req,
	fuse_ino_t ino,
	size_t size,
	off_t offset,
	struct fuse_file_info *fi,
	int plus
) {
	DIR *dir = (DIR *)fi->fh;
	char *buf;
	Ino parent = fuse2ino(ino);

	(void)ino;

	buf = malloc(size);
	if (!buf) {
		errno = ENOMEM;
		goto error;
	}

	seekdir(dir, offset);
	size_t fill = 0;
	for (;;) {
		size_t ent_size;
		struct dirent *ent = readdir(dir);
		if (ent == NULL)
			goto error;
		// Skip these, the driver will return them for us.
		if (is_dot_or_dotdot(ent->d_name))
			continue;
		Ino child = ino_child(parent, ent->d_name);
		if (plus) {
			struct fuse_entry_param e;
			errno = fill_entry(child, &e);
			if (errno)
				goto error;
			ino_ref(child, 1);
			ent_size = fuse_add_direntry_plus(
				req, buf, size, ent->d_name, &e, telldir(dir)
			);
		} else {
			struct stat stbuf
				= { .st_ino = ino2fuse(child), .st_mode = ent->d_type << 12 };
			ent_size = fuse_add_direntry(
				req, buf, size, ent->d_name, &stbuf, telldir(dir)
			);
		}

		fill += ent_size;
		if (fill > size) {
			fill -= ent_size;
			break;
		}
	}

	errno = 0;
error:
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
	fm_do_readdir(req, ino, size, offset, fi, 0);
}

static void fm_readdirplus(
	fuse_req_t req,
	fuse_ino_t ino,
	size_t size,
	off_t offset,
	struct fuse_file_info *fi
) {
	fm_do_readdir(req, ino, size, offset, fi, 1);
}

static void
fm_releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
	closedir((DIR *)fi->fh);
	fuse_reply_err(req, 0);
}

static int fm_do_open(Ino ino, mode_t mode, struct fuse_file_info *fi) {
	int fd = open(inode(ino)->name, fi->flags & ~O_NOFOLLOW, mode);
	if (fd == -1)
		return errno;
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
	int fd;
	Ino child = ino_child(fuse2ino(parent), name);
	struct fuse_entry_param e;
	int err;

	fi->flags |= O_CREAT;
	err = fm_do_open(child, mode, fi);
	if (err != 0) {
		fuse_reply_err(req, err);
		return;
	}

	err = fill_entry(child, &e);
	if (err)
		fuse_reply_err(req, err);
	else
		fm_reply_create(child, req, &e, fi);
}

static void fm_fsyncdir(
	fuse_req_t req, fuse_ino_t ino, int datasync, struct fuse_file_info *fi
) {
	int res;
	int fd = dirfd((DIR *)fi->fh);
	(void)ino;
	if (datasync)
		res = fdatasync(fd);
	else
		res = fsync(fd);
	fuse_reply_err(req, res == -1 ? errno : 0);
}

static void fm_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
	fm_do_open(fuse2ino(ino), 0, fi);
	fuse_reply_open(req, fi);
}

static void
fm_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
	(void)ino;
	close(fi->fh);
	fuse_reply_err(req, 0);
}

static void
fm_flush(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
	int res;
	(void)ino;
	res = close(dup(fi->fh));
	fuse_reply_err(req, res == -1 ? errno : 0);
}

static void fm_fsync(
	fuse_req_t req, fuse_ino_t ino, int datasync, struct fuse_file_info *fi
) {
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
	int res;
	struct statvfs stbuf;

	res = statvfs(inode(fuse2ino(ino))->path, &stbuf);
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
	int err = EOPNOTSUPP;
	(void)ino;

#ifdef HAVE_FALLOCATE
	err = fallocate(fi->fh, mode, offset, length);
	if (err < 0)
		err = errno;

#elif defined(HAVE_POSIX_FALLOCATE)
	if (mode) {
		fuse_reply_err(req, EOPNOTSUPP);
		return;
	}

	err = posix_fallocate(fi->fh, offset, length);
#endif

	fuse_reply_err(req, err);
}

static void
fm_flock(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi, int op) {
	int res;
	(void)ino;

	res = flock(fi->fh, op);

	fuse_reply_err(req, res == -1 ? errno : 0);
}

static void
fm_getxattr(fuse_req_t req, fuse_ino_t ino, const char *name, size_t size) {
	char *value = NULL;
	ssize_t ret;
	int saverr;

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
	char *value = NULL;
	ssize_t ret;
	int saverr;

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
	ssize_t ret;
	if (0 == strcmp(name, "fusemake.depend")) {
		fuse_reply_err(req, 0);
		return;
	}

	ret = setxattr(inode(fuse2ino(ino))->path, name, value, size, flags);
	fuse_reply_err(req, ret == -1 ? errno : 0);
}

static void fm_removexattr(fuse_req_t req, fuse_ino_t ino, const char *name) {
	ssize_t ret;
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

// Arguments:
// --init builder: initialise the current directory for building with builder.
// --dpepend <args>: within the build process, declare a dependency on each of
// args and write them to stdout on newlines.
// --depend: without arguments, use lines from stdin.
static char *fuse_args[] = { "fusemake" };
int main(int argc, char *argv[]) {
	struct fuse_args args = FUSE_ARGS_INIT(LENGTH(fuse_args), fuse_args);
	struct fuse_session *se;
	struct fuse_loop_config config;
	int ret = -1;

	/* Don't mask creation mode, kernel already did that */
	umask(0);
	getcwd(project_root, LENGTH(project_root));
	project_root_name = strrchr(project_root, '/') + 1;

	se = fuse_session_new(&args, &fm_oper, sizeof(fm_oper), NULL);
	if (se == NULL)
		goto err_out1;

	if (fuse_set_signal_handlers(se) != 0)
		goto err_out2;

	if (fuse_session_mount(se, ".fusemake/mount") != 0)
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
