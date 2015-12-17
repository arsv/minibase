#include <bits/syscall.h>
#include <syscall4.h>

struct stat;

inline static long sysfstatat(int dirfd, const char *path,
		struct stat *buf, int flags)
{
	return syscall4(__NR_newfstatat, dirfd, (long)path, (long)buf, flags);
}
