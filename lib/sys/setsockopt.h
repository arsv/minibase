#include <syscall.h>

inline static long syssetsockopt(int fd, int lvl, int opt, void* val, int len)
{
	return syscall5(__NR_setsockopt, fd, lvl, opt, (long)val, len);
}
