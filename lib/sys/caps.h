#include <bits/types.h>
#include <bits/caps.h>
#include <syscall.h>

#define LINUX_CAPABILITY_VERSION 0x20080522
#define LINUX_CAPABILITY_U32S   2

struct cap_header {
	uint32_t version;
	int pid;
};

struct cap_data {
	uint32_t effective;
	uint32_t permitted;
	uint32_t inheritable;
};

inline static long sys_capset(struct cap_header* ch, const struct cap_data* cd)
{
	return syscall2(NR_capset, (long)ch, (long)cd);
}

inline static long sys_capget(struct cap_header* ch, struct cap_data* cd)
{
	return syscall2(NR_capget, (long)ch, (long)cd);
}
