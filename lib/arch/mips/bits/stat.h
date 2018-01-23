#ifndef __BITS_STAT_H__
#define __BITS_STAT_H__

#include <bits/types.h>
#include <bits/time.h>

/* ref. linux/arch/mips/include/uapi/asm/stat.h */

struct stat {
	uint32_t dev;
	uint32_t __0[3];
	uint64_t ino;
	uint32_t mode;
	uint32_t nlink;
	 int32_t uid;
	 int32_t gid;
	uint32_t rdev;
	uint32_t __1[3];
	 int64_t size;
	struct timespec atime;
	struct timespec mtime;
	struct timespec ctime;
	uint32_t blksize;
	uint32_t __2;
	 int64_t blocks;
};

#endif
