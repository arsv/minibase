#include <syscall.h>
#include <bits/sysinfo.h>

inline static long syssysinfo(struct sysinfo* buf)
{
	return syscall1(__NR_sysinfo, (long)buf);
}
