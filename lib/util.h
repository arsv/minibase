#ifndef __UTIL_H__
#define __UTIL_H__

#include <bits/types.h>
#include <bits/stdio.h>
#include <bits/null.h>

#define ARRAY_SIZE(a) (sizeof(a)/sizeof(*a))
#define ARRAY_END(a) (a + ARRAY_SIZE(a))
#define offsetof(t, f) __builtin_offsetof(t, f)

int argbits(const char* key, const char* arg);
int argsumlen(int argc, char** argv);
char* argsmerge(char* buf, char* end, int argc, char** argv);

const char* basename(const char* path);
char* getenv(char** envp, const char* key);

typedef int (*qcmp)(const void* a, const void* b, long p);
void qsort(void* base, size_t nmemb, size_t size, qcmp cmp, long data);

long writeall(int fd, char* buf, long len);

long execvpe(char* file, char** argv, char** envp);

int getifindex(int fd, char* ifname);

void warn(const char* msg, const char* obj, int err);
void fail(const char* msg, const char* obj, int err) __attribute__((noreturn));
void _exit(int) __attribute__((noreturn));

/* There's a common routine when we need to fail() in case given
   syscall returns an error. In some cases we may be interested
   in the non-error return as well, as in

	long fd = xchk( sysopen(filename, ...),
			"cannot open", filename);

   Perlish equivalent would be $fd = open() or die;

   The function is so small it does not make sense to link it. */

static inline long xchk(long ret, const char* msg, const char* obj)
{
	if(ret < 0)
		fail(msg, obj, ret);
	else
		return ret;
}

#endif
