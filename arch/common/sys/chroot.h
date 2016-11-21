#include <syscall.h>

inline static long syschroot(const char* dir)
{
	return syscall1(__NR_chroot, (long)dir);
}
