#include "header.h"
#include <pthread.h>
#include <unistd.h>

sem_t process_available;
char *builder_path;
pthread_t process_manager_thread;

static void *process_manager(void *ignored) { return NULL; }

void process_manager_init(int process_count) {
	sem_init(&process_available, 0, active_processes);
	pthread_create(&process_manager_thread, NULL, process_manager, NULL);
}
void process_manager_free() {
  //TODO: Kill manager thread.
	sem_destroy(&process_available);
}

static void *build(void *process_as_ptr) {
	int process = (intptr_t)process_as_ptr;
	chdir(MOUNT_POINT);
	char buf[16];
	sprintf(buf, "%d", process);
	chdir(buf);
	sem_wait(&process_available);
	return NULL;
}
void queue_build(Inode *i, Callback cb, CallbackArgs cb_args) {
	const char *path = inode_path(i);
	Ino root = add_root(path);
	pthread_t thread;
	pthread_create(&thread, NULL, build, (void *)(intptr_t)root.process);
}
