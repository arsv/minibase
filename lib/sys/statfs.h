#include <bits/types.h>
#include <syscall.h>

struct statfs {
	ulong type;
	ulong bsize;
	ulong blocks;
	ulong bfree;
	ulong bavail;
	ulong files;
	ulong ffree;
	int fsid;
	ulong namelen;
	ulong frsize;
	ulong flags;
	long spare[4];
};

inline static long sys_statfs(const char* path, struct statfs* st)
{
	return syscall2(NR_statfs, (long)path, (long)st);
}
