#include <syscall.h>
#include <bits/types.h>

#define FALLOC_FL_KEEP_SIZE        1
#define FALLOC_FL_PUNCH_HOLE       2
#define FALLOC_FL_COLLAPSE_RANGE   8
#define FALLOC_FL_ZERO_RANGE      16

inline static long sysfallocate(int fd, int mode, uint64_t offset, uint64_t len)
{
	return syscall4(__NR_fallocate, fd, mode, offset, len);
}
