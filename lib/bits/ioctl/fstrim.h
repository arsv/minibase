#include <bits/ioctl.h>
#include <bits/ints.h>

struct fstrim_range {
	uint64_t start;
	uint64_t len;
	uint64_t minlen;
};

#define FITRIM _IOWR('X', 121, struct fstrim_range)
