#include <syscall.h>
#include <bits/fcntl.h>
#include <bits/time.h>

#define UTIME_NOW	((1l << 30) - 1l)
#define UTIME_OMIT	((1l << 30) - 2l)

inline static long sysutimensat(int dirfd, char* path,
		const struct timespec times[2], int flags)
{
	return syscall4(__NR_utimensat, dirfd, (long)path, (long)times, flags);
}
