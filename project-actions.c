#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define eprintf(...) fprintf(stderr, __VA_ARGS__)
#define IO_ERROR "IO error.\n"
#define CHECK_IO(op, ...) \
	{ \
		if (!(op)) { \
			int ret = errno; \
			eprintf(__VA_ARGS__); \
			exit(ret); \
		} \
	}

int main(int arc, char **argv) {
#define ORIGIN_LEN (1 << 20)
	static char ORIGIN[ORIGIN_LEN];
	CHECK_IO(getcwd(ORIGIN, ORIGIN_LEN), IO_ERROR);
  setenv("PA_ORIGIN", ORIGIN, true);
	int depth = 0;
	// Ignore trailing /
	for (int i = 0; ORIGIN[i] != '\0' && ORIGIN[i + 1] != '\0'; ++i)
		depth += ORIGIN[i] == '/';
	for (; depth >= 0; --depth, chdir("..")) {
		if (0 == access("project-actions", X_OK)) {
			argv[0] = "project-actions";
			execv(argv[0], argv);
		} else if (0 == access("project-actions", F_OK)) {
			CHECK_IO(getcwd(ORIGIN, ORIGIN_LEN), IO_ERROR);
			eprintf("%s/project-actions wasn't executable.\n", ORIGIN);
      return ENOEXEC;
		} else if (errno != ENOENT) {
			CHECK_IO(false, IO_ERROR);
		}
	}
	eprintf("Could not find project-actions script.\n");
	exit(ENOENT);
}
