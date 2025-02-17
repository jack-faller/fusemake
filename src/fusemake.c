/*
  Based on the pass-through example given by libfuse.
*/
// TODO: error handling on reply functions.

#include "header.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/xattr.h>
#include <unistd.h>

#include "../out/string_defs.h"

char ROOT[PATH_MAX];
const char *ROOT_NAME;

// TODO: remember to handle symlinked dependencies.
// TODO: remember to block for dependencies.

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
		eprintf("Too many arguments to fusemake " OP ".\n" \
		        "\tUsage: fusemake " OP " /path/to/builder\n"); \
		return E2BIG; \
	}
	int ret = -1;
	if (argc > 1 && 0 == strcmp(argv[1], "--help")) {
		eprintf("%s\n", usage);
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
		active_processes = sysconf(_SC_NPROCESSORS_ONLN);
		process_manager_init(active_processes);

		validate_executable(BUILDER);
		struct fuse_args args = FUSE_ARGS_INIT(LENGTH(fuse_args), fuse_args);
		struct fuse_session *se;

		/* Don't mask creation mode, kernel already did that */
		umask(0);
		// Add process 0.
		add_root(NULL);

		se = fuse_session_new(&args, &fm_oper, sizeof(fm_oper), NULL);
		if (se == NULL)
			goto err_out1;

		if (fuse_set_signal_handlers(se) != 0)
			goto err_out2;

		if (fuse_session_mount(se, MOUNT_POINT) != 0)
			goto err_out3;

		set_cloexec(fuse_session_fd(se), true);

		fuse_daemonize(true);

		ret = fuse_session_loop(se);

		fuse_session_unmount(se);
	err_out3:
		fuse_remove_signal_handlers(se);
	err_out2:
		fuse_session_destroy(se);
	err_out1:
		fuse_opt_free_args(&args);
		process_manager_free();

		return ret ? 1 : 0;
	}
}
