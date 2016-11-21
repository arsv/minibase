#include <syscall.h>
#include <bits/stat.h>

inline static long sysfstat(int fd, struct stat* st)
{
	return syscall2(__NR_fstat, fd, (long)st);
}
