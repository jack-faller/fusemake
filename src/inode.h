#define FUSE_USE_VERSION 34
#include <fuse_lowlevel.h>
#include "utils.h"

typedef struct Inode Inode;

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


extern int processes_len, active_processes;
Ino fuse2ino(fuse_ino_t ino);
fuse_ino_t ino2fuse(Ino ino);
void ino_ref(Ino ino, int count);
Ino add_root(const char *target);
int count_active_roots();
Inode *inode(Ino ino);
const char *inode_path(Inode *i);
const char *inode_name(Inode *i);
int ino_generation(Ino ino);
bool root_active(int root);
Ino ino_child(Ino parent, const char *name);
