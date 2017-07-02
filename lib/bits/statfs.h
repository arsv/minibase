#ifndef __BITS_STATFS_H__
#define __BITS_STATFS_H__

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

#endif
