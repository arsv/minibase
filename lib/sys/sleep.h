#include <syscall.h>
#include <bits/time.h>

inline static long sys_pause(void)
{
	return syscall4(__NR_ppoll, 0, 0, 0, 0);
}

inline static long sys_nanosleep(struct timespec* req, struct timespec* rem)
{
	return syscall2(__NR_nanosleep, (long)req, (long)rem);
}
