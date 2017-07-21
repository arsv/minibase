#ifndef __BITS_IOVEC_H__
#define __BITS_IOVEC_H__

#include <bits/types.h>

struct iovec {
	void *base;
	size_t len;
};

#endif
