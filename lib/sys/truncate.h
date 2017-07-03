#include <syscall.h>
#include <bits/fcntl.h>
#include <bits/time.h>

#define FALLOC_FL_KEEP_SIZE        1
#define FALLOC_FL_PUNCH_HOLE       2
#define FALLOC_FL_COLLAPSE_RANGE   8
#define FALLOC_FL_ZERO_RANGE      16

inline static long sys_fallocate(int fd, int mode, uint64_t offset, uint64_t len)
{
	return syscall4(__NR_fallocate, fd, mode, offset, len);
}

inline static long sys_truncate(const char* path, uint64_t size)
{
	return syscall2(__NR_truncate, (long)path, size);
}

inline static long sys_ftruncate(int fd, uint64_t size)
{
	return syscall2(__NR_ftruncate, fd, size);
}
