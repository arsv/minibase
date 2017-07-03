#include <syscall.h>

struct utsname {
	char sysname[65];
	char nodename[65];
	char release[65];
	char version[65];
	char machine[65];
	char domainname[65];
};

struct sysinfo {
	unsigned long uptime;
	unsigned long loads[3];
	unsigned long totalram;
	unsigned long freeram;
	unsigned long sharedram;
	unsigned long bufferram;
	unsigned long totalswap;
	unsigned long freeswap;
	unsigned short procs, pad;
	unsigned long totalhigh;
	unsigned long freehigh;
	unsigned mem_unit;
	char __reserved[256];
};

inline static long sys_uname(struct utsname* buf)
{
	return syscall1(__NR_uname, (long)buf);
}

inline static long sys_info(struct sysinfo* buf)
{
	return syscall1(__NR_sysinfo, (long)buf);
}
