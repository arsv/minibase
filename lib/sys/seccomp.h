#include <syscall.h>

#define SECCOMP_SET_MODE_STRICT	0
#define SECCOMP_SET_MODE_FILTER	1

struct seccomp {
	unsigned short len;
	char* buf;
};

inline static long sys_seccomp(int cmd, int flags, void* ptr)
{
	return syscall3(NR_seccomp, cmd, flags, (long)ptr);
}
