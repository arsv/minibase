#include <syscall.h>

inline static long sysgetsockopt(int fd, int level,
		int opt, void* val, int* len)
{
	return syscall5(__NR_getsockopt, fd, level, opt, (long)val, (long)len);	
}
