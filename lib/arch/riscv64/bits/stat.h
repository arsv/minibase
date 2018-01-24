#ifndef __BITS_STAT_H__
#define __BITS_STAT_H__

#include <bits/types.h>
#include <bits/time.h>

struct stat {
	uint64_t dev;
	uint64_t ino;
	uint32_t nlink;
	uint32_t __0;
	uint32_t mode;
	int32_t uid;
	int32_t gid;
	uint64_t rdev;
	int64_t size;
	int32_t blksize;
	uint32_t __2;
	int64_t blocks;
	struct timespec atime;
	struct timespec mtime;
	struct timespec ctime;
	uint64_t __3;
	uint64_t __4;
	uint64_t __5;
};

#endif
