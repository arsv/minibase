#include <syscall.h>

inline static long syschdir(const char* path)
{
	return syscall1(__NR_chdir, (long)path);
}
