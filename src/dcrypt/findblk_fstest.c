#include <sys/fcntl.h>
#include <sys/file.h>
#include <string.h>
#include <format.h>
#include <util.h>

#include "findblk.h"

/* IV-based passphrase validity test may be positive even if
   if passphrase is incorrect, especially if the test IV is short.

   To provide so extra assurance that the unwrapped the key is correct
   without compromising the securing of the wrap, we optionally test
   decrypted data for FS superblock markers.

   This should be a quick test with certain negative but false positives
   are generally ok. */

static int read_at_off(int fd, long off, char* buf, int size)
{
	int ret;

	if((ret = sys_lseek(fd, off, 0)) < 0)
		return ret;
	if((ret = sys_read(fd, buf, size)) < 0)
		return ret;
	if(ret < size)
		return -EINTR;

	return ret;
}

static int test_ext_fs(int fd)
{
	char buf[1024];
	char test[] = { 0x53, 0xEF };
	int ret;

	if((ret = read_at_off(fd, 1024, buf, sizeof(buf))) < 0)
		return ret;

	return memcmp(buf + 0x38, test, sizeof(test));
}

static int test_iso9660(int fd)
{
	char buf[1024];
	char test[] = { 'C', 'D', '0', '0', '1' };
	int ret;

	if((ret = read_at_off(fd, 32768, buf, sizeof(buf))) < 0)
		return ret;

	return memcmp(buf + 1, test, sizeof(test));
}

static const struct fs {
	char name[8];
	int (*check)(int fd);
} tests[] = {
	{ "ext2",    test_ext_fs  },
	{ "ext3",    test_ext_fs  },
	{ "ext4",    test_ext_fs  },
	{ "iso9660", test_iso9660 }
};

static int open_part(struct part* pt)
{
	char* pref = "/dev/";
	char* name = pt->name;
	int fd;

	if(pt->fd > 0)
		return pt->fd;

	FMTBUF(p, e, path, strlen(pref) + strlen(name) + 4);
	p = fmtstr(p, e, pref);
	p = fmtstr(p, e, name);
	FMTEND(p);

	if((fd = sys_open(path, O_RDONLY)) < 0)
		quit("open", path, fd);

	pt->fd = fd;
	
	return fd;
}

static int check_part(struct part* pt)
{
	int fd;
	const struct fs* p;
	
	fd = open_part(pt);

	for(p = tests; p < tests + ARRAY_SIZE(tests); p++)
		if(!strncmp(p->name, pt->fs, sizeof(p->name)))
			return p->check(fd);
	
	quit("cannot test for filesystem named", pt->fs, 0);
}

int check_partitions(void)
{
	struct part* pt;
	int ret = 0;

	for(pt = parts; pt < parts + nparts; pt++)
		if(!(pt->fs[0]))
			continue;
		else if((ret = check_part(pt)) < 0)
			break;

	return ret;
}
