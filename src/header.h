#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>

#define FUSE_USE_VERSION 34
#include <fuse_lowlevel.h>

// Used by fusemake --depend.
#define DEPEND_ATTR "fusemake.depend"
// When a build process finishes, the thread calls this to make sure everything
// happens single-threaded in the FUSE main thread.
#define TERMINATE_ATTR "fusemake.terminate"
#define LENGTH(ARRAY) (sizeof(ARRAY) / sizeof((ARRAY)[0]))
#define DOT_FUSEMAKE ".fusemake"
#define MOUNT_POINT DOT_FUSEMAKE "/mount"
#define MOUNT_TO_ROOT "../../.."
#define BUILDER DOT_FUSEMAKE "/builder"

#define eprintf(...) fprintf(stderr, __VA_ARGS__)
#define DEBUG(...) eprintf(__VA_ARGS__)

// Who needs more than 65k processes?
#define PROCESS_MAX (1 << 16)
// Inode one the root, so shift ino to exclude zero and one.
#define INO_PROCESS(ino) (((ino) - 2) % PROCESS_MAX)
#define INO_ID(ino) (((ino) - 2) / PROCESS_MAX)
#define INO(process, id) ((id) * PROCESS_MAX + (process) + 2)
typedef struct Ino {
	int process;
	int id;
} Ino;

typedef struct Inode Inode;

// file_system.c
extern const struct fuse_lowlevel_ops fm_oper;

// process_manager.c
extern sem_t process_available;
extern char *builder_path;

// inode.c
extern int processes_len, active_processes;
extern char ROOT[];
Ino fuse2ino(fuse_ino_t ino);
fuse_ino_t ino2fuse(Ino ino);
void ino_ref(Ino ino, int count);
Ino add_root(const char *target);
int count_active_roots();
Inode *inode(Ino ino);
char *inode_path(Inode *i);
char *inode_name(Inode *i);
int ino_generation(Ino ino);
bool root_active(int root);
Ino ino_child(Ino parent, const char *name);

// fusemake.c
/* void make(Inode *i, bool opened, callback); */
