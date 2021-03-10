#ifndef __UTIL_H__
#define __UTIL_H__

#include <cdefs.h>

int argbits(char* key, char* arg);

char* basename(char* path);
char* getenv(char** envp, char* key);

typedef int (*qcmp2)(void* a, void* b);
typedef int (*qcmp3)(void* a, void* b, long opts);

void qsortp(void* ptrs, size_t n, qcmp2 cmp);
void qsortx(void* ptrs, size_t n, qcmp3 cmp, long opts);

long writeall(int fd, void* buf, long len);

long execvpe(char* file, char** argv, char** envp);

int getifindex(int fd, char* ifname);

void warn(const char* msg, const char* obj, int err);
void fail(const char* msg, const char* obj, int err) noreturn;
void _exit(int) noreturn;

#endif
