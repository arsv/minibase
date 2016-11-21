#include <syscall.h>
#include <bits/time.h>

inline static long sysnanosleep(struct timespec* req, struct timespec* rem)
{
	return syscall2(__NR_nanosleep, (long)req, (long)rem);
}
