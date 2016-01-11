#include <bits/syscall.h>
#include <bits/fcntl.h>
#include <syscall4.h>

struct stat;

inline static long syslstat(const char *path, struct stat *st)
{
	return syscall4(__NR_fstatat,
			AT_FDCWD, (long)path, (long)st, AT_SYMLINK_NOFOLLOW);
}
