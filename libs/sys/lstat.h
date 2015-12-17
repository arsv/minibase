#include <bits/syscall.h>
#include <syscall2.h>

struct stat;

inline static long syslstat(const char *path, struct stat *st)
{
	return syscall2(__NR_lstat, (long)path, (long)st);
}
