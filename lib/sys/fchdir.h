#include <syscall.h>

inline static long sysfchdir(int fd)
{
	return syscall1(__NR_fchdir, fd);
}
