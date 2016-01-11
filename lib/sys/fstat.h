#include <bits/syscall.h>
#include <syscall.h>

struct stat;

inline static long sysfstat(int fd, struct stat* st)
{
	return syscall2(__NR_fstat, fd, (long)st);
}
