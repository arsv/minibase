#include <syscall.h>

inline static long syssendfile(int ofd, int ifd, uint64_t* offset, unsigned long count)
{
	return syscall4(__NR_sendfile, ofd, ifd, (long)offset, count);
}
