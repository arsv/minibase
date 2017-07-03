#include <syscall.h>
#include <bits/errno.h>
#include <bits/fcntl.h>
#include <bits/time.h>

inline static long sys_symlink(const char *target, const char *path)
{
	return syscall3(__NR_symlinkat, (long)target, AT_FDCWD, (long)path);
}

inline static long sys_symlinkat(const char *target, int at, const char *path)
{
	return syscall3(__NR_symlinkat, (long)target, at, (long)path);
}

inline static long sys_readlink(const char* path, char* buf, long len)
{
#ifdef __NR_readlinkat
	return syscall4(__NR_readlinkat, AT_FDCWD, (long)path, (long)buf, len);
#else
	return syscall3(__NR_readlink, (long)path, (long)buf, len);
#endif
}

inline static long sys_readlinkat(int at, const char* path, char* buf, long len)
{
#ifdef __NR_readlinkat
	return syscall4(__NR_readlinkat, AT_FDCWD, (long)path, (long)buf, len);
#else
	return -ENOSYS;
#endif
}
