#ifndef __BITS_STAT_H__
#define __BITS_STAT_H__

#include <bits/ints.h>
#include <bits/time.h>

struct stat {
	uint64_t dev;
	uint64_t ino;
	uint32_t mode;
	uint32_t nlink;
	uint32_t uid;
	uint32_t gid;
	uint64_t rdev;
	uint64_t __pad1;
	 int64_t size;
	 int32_t blksize;
	 int32_t __pad2;
	 int64_t blocks;
	struct timespec atime;
	struct timespec mtime;
	struct timespec ctime;
	uint32_t __unused4;
	uint32_t __unused5;
};

#endif
