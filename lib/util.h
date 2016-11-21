#ifndef __UTIL_H__
#define __UTIL_H__

#include <bits/types.h>
#include <bits/time.h>
#include <null.h>

#include <bits/stdio.h>

int argbits(const char* key, const char* arg);
int argsumlen(int argc, char** argv);
char* argsmerge(char* buf, char* end, int argc, char** argv);

const char* basename(const char* path);
char* getenv(char** envp, const char* key);

typedef int (*qcmp)(const void* a, const void* b, long p);
void qsort(void* base, size_t nmemb, size_t size, qcmp cmp, long data);

void tm2tv(struct tm* tm, struct timeval* tv);
void tv2tm(struct timeval* tv, struct tm* tm);

long writeall(int fd, char* buf, long len);

#endif
