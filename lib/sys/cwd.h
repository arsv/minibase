#include <syscall.h>
#include <bits/types.h>

inline static long sys_chdir(const char* path)
{
	return syscall1(__NR_chdir, (long)path);
}

inline static long sys_fchdir(int fd)
{
	return syscall1(__NR_fchdir, fd);
}

inline static long sys_getcwd(char* buf, size_t size)
{
	return syscall2(__NR_getcwd, (long)buf, size);
}

inline static long sys_chroot(const char* dir)
{
	return syscall1(__NR_chroot, (long)dir);
}
