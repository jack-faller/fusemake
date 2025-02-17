#ifndef FUSEMAKE_H
#define FUSEMAKE_H

#include "utils.h"

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

// fusemake.c
/* void make(Inode *i, bool opened, callback); */
extern char ROOT[];
extern const char *ROOT_NAME;

#endif
