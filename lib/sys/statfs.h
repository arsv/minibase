#include <syscall.h>

struct statfs {
	long type;
	long bsize;
	unsigned long blocks;
	unsigned long bfree;
	unsigned long bavail;
	unsigned long files;
	unsigned long ffree;
	int fsid;
	long namelen;
	long frsize;
	long flags;
	long spare[4];
};

inline static long sys_statfs(const char* path, struct statfs* st)
{
	return syscall2(NR_statfs, (long)path, (long)st);
}
