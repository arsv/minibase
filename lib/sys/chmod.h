#include <syscall.h>

inline static long syschmod(const char* filename, int mode)
{
	return syscall2(__NR_chmod, (long)filename, mode);
}
