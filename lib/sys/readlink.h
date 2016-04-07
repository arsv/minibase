#include <syscall.h>

inline static long sysreadlink(const char* filename, char* buf, long len)
{
	return syscall3(__NR_readlink, (long)filename, (long)buf, len);
}
