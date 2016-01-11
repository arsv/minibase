#include <bits/syscall.h>
#include <syscall1.h>

inline static long syschroot(const char* dir)
{
	return syscall1(__NR_chroot, (long)dir);
}
