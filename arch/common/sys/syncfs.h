#include <syscall.h>

inline static long syssyncfs(int fd)
{
	return syscall1(__NR_syncfs, fd);
}
