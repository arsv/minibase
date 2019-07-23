#include <syscall.h>

#define GRND_NONBLOCK   (1<<0)
#define GRND_RANDOM     (1<<1)

inline static long sys_getrandom(void* buf, size_t len, int flags)
{
	return syscall3(NR_getrandom, (long)buf, len, flags);
}
