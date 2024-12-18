/*
  Based on the pass-through example given by libfuse.
*/
// TODO: error handling on reply functions.
// TODO: zero argument version of --init that creates a skeleton make.sh.

#include "header.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/xattr.h>
#include <unistd.h>

#include "../out/string_defs.h"

void *build(void *process_as_ptr) {
	int process = (intptr_t)process_as_ptr;
	chdir(MOUNT_POINT);
	char buf[16];
	sprintf(buf, "%d", process);
	chdir(buf);
	sem_wait(&process_available);
	return NULL;
}
void spawn_build(const char *path) {
	Ino root = add_root(path);
	pthread_t thread;
	pthread_create(&thread, NULL, build, (void *)(intptr_t)root.process);
}

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
void chdir_to_root() {
	static char path[PATH_MAX];
	CHECK_IO(NULL != getcwd(path, LENGTH(path)), IO_ERROR);
	int depth = 1;
	// Ignore trailing /
	for (int i = 0; path[i] != '\0' && path[i + 1] != '\0'; ++i)
		depth += path[i] == '/';
	for (; depth >= 0; --depth) {
		if (0 == access(DOT_FUSEMAKE, R_OK | W_OK | X_OK)) {
			CHECK_IO(NULL != getcwd(path, LENGTH(path)), IO_ERROR);
			setenv("FUSEMAKE_NO_DEPEND", path, true);
			return;
		} else if (errno != ENOENT) {
			CHECK_IO(false, IO_ERROR);
		}
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
		const static char *parameters_flat[] = {
			"--depend",
			"<args>",
			"Build each of args asynchronously and write the names to stdout.",
			"Should only be called during the build process.",
			NULL,
			"--depend",
			"",
			"Without arguments, use lines from stdin.",
			NULL,
			"--processes",
			"<count>",
			"Allow approximately count build processes.",
			"Defaults to the number of processors on the device.",
			NULL,
			"--set-builder",
			"<builder>",
			"Set the builder.",
			NULL,
			"--init",
			"<builder>",
			"Initialise the current directory for building with builder.",
			NULL,
		};
		static const char **parameters[LENGTH(parameters_flat)];
		int parameter_count = 0;
		for (int i = 0; i < LENGTH(parameters_flat);) {
			parameters[parameter_count++] = &parameters_flat[i];
			// Move to start of next sub-array.
			while (parameters_flat[i++] != NULL)
				;
		}
		printf("%s\n", usage);
		int width_1 = 0, width_2 = 0;
		for (int i = 0; i < parameter_count; ++i)
			width_1 = MAX(width_1, strlen(parameters[i][0])),
			width_2 = MAX(width_2, strlen(parameters[i][1]));
		for (int i = 0; i < parameter_count; ++i) {
#define LINE(A, B, C) \
	printf("  %-*s%-*s%s\n", width_1 + 1, A, width_2 + 2, B, C);
			LINE(parameters[i][0], parameters[i][1], parameters[i][2]);
			for (int j = 3; parameters[i][j] != NULL; ++j)
				LINE("", "", parameters[i][j]);
#undef LINE
		}
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
				"Error creating symlink %s, please remove .fusemake and try "
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
		active_processes = sysconf(_SC_NPROCESSORS_ONLN);
		sem_init(&process_available, 0, active_processes);
		chdir_to_root();
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

		fuse_daemonize(true);

		ret = fuse_session_loop(se);

		fuse_session_unmount(se);
	err_out3:
		fuse_remove_signal_handlers(se);
	err_out2:
		fuse_session_destroy(se);
	err_out1:
		fuse_opt_free_args(&args);
		sem_destroy(&process_available);

		return ret ? 1 : 0;
	}
}
