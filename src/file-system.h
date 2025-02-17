#define FUSE_USE_VERSION 34
#include <fuse_lowlevel.h>
#include "utils.h"

// file_system.c
extern const struct fuse_lowlevel_ops fm_oper;
int set_cloexec(int fd, bool value);
