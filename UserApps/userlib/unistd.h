#ifndef _UNISTD_H_
#define _UNISTD_H_

#include "userlib.h"

int open(const char* pathname, int flags, ...);
int close(int fd);
int read(int fd, void* buf, unsigned int count);
int write(int fd, const void* buf, unsigned int count);
int lseek(int fd, long offset, int whence);
int unlink(const char* pathname);
char* getcwd(char* buf, size_t size);
int chmod(const char* path, int mode);

#endif
