#include <stdbool.h>
#include <stdint.h>

#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

typedef int pid_t;

void syscall_init(void);

bool check_addr(void *addr);

void halt(void);
void exit(int status);
int wait(pid_t pid);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
// pid_t fork(const char *thread_name, struct intr_frame *if_);
int exec(const char *cmd_line);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);

#endif /* userprog/syscall.h */
