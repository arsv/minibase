#include <syscall.h>
#include <bits/types.h>
#include <bits/fcntl.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

inline static long sys_fcntl(int fd, int cmd)
{
	return syscall2(__NR_fcntl, fd, cmd);
}

inline static long sys_fcntl3(int fd, int cmd, int arg)
{
	return syscall3(__NR_fcntl, fd, cmd, arg);
}

inline static long sys_dup2(int fda, int fdb)
{
	return syscall3(__NR_dup3, fda, fdb, 0);
}

inline static long sys_lseek(int fd, uint64_t off, int whence)
{
	return syscall3(__NR_lseek, fd, off, whence);
}
