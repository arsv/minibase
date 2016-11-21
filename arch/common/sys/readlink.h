#include <syscall.h>
#include <bits/fcntl.h>

inline static long sysreadlink(const char* filename, char* buf, long len)
{
#ifdef __NR_readlinkat
	return syscall4(__NR_readlinkat, AT_FDCWD, (long)filename, (long)buf, len);
#else
	return syscall3(__NR_readlink, (long)filename, (long)buf, len);
#endif
}
