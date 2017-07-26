#ifndef __BITS_STAT_H__
#define __BITS_STAT_H__

#include <bits/ints.h>
#include <bits/time.h>

struct stat {
	uint64_t dev;
	uint64_t ino;
	uint32_t nlink;
	uint32_t __pad0;
	uint32_t mode;
	uint32_t uid;
	uint32_t gid;
	uint64_t rdev;
	uint64_t size;
	uint32_t blksize;
	uint32_t __pad2;
	uint64_t blocks;
	struct timespec atime;
	struct timespec mtime;
	struct timespec ctime;
	uint64_t __unused3;
	uint64_t __unused4;
	uint64_t __unused5;
};

#endif
