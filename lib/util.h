#ifndef __UTIL_H__
#define __UTIL_H__

#include <cdefs.h>

int argbits(const char* key, const char* arg);
int argsumlen(int argc, char** argv);
char* argsmerge(char* buf, char* end, int argc, char** argv);

char* basename(char* path);
char* getenv(char** envp, const char* key);

typedef int (*qcmp2)(const void* a, const void* b);
typedef int (*qcmp3)(const void* a, const void* b, long opts);

void qsort(void* base, size_t nmemb, size_t size, qcmp2 cmp);
void qsortx(void* base, size_t nmemb, size_t size, qcmp3 cmp, long opts);

long writeall(int fd, char* buf, long len);

long execvpe(char* file, char** argv, char** envp);

int getifindex(int fd, char* ifname);

void warn(const char* msg, const char* obj, int err);
void fail(const char* msg, const char* obj, int err) noreturn;
void _exit(int) noreturn;

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
