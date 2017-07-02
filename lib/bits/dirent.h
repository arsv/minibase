#ifndef __BITS_DIRENT_H__
#define __BITS_DIRENT_H__

#include <bits/types.h>

#define DT_UNKNOWN  0
#define DT_FIFO     1
#define DT_CHR      2
#define DT_DIR      4
#define DT_BLK      6
#define DT_REG      8
#define DT_LNK     10
#define DT_SOCK    12
#define DT_WHT     14

struct dirent64 {
	ino64_t ino;
	off64_t	off;
	uint16_t reclen;
	uint8_t type;
	char name[];
};

#endif
