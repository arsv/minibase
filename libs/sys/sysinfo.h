#include <bits/syscall.h>
#include <syscall1.h>

struct sysinfo;

inline static long syssysinfo(struct sysinfo* buf)
{
	return syscall1(__NR_sysinfo, (long)buf);
}
