#include <bits/ints.h>

#define INPUT_MAJOR  13
#define DRI_MAJOR   226

/* dev_t is 64-bit in current kernels! */

static inline long major(uint64_t dev)
{
	return ((dev >>  8) & 0x00000FFF)
	     | ((dev >> 32) & 0xFFFFF000);
}

static inline long minor(uint64_t dev)
{
	return ((dev >>  0) & 0x000000FF)
	     | ((dev >> 12) & 0xFFFFFF00);
}
