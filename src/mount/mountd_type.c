#include <sys/file.h>
#include <sys/fprop.h>

#include <nlusctl.h>
#include <cmsg.h>
#include <string.h>
#include <format.h>
#include <util.h>

#include "common.h"
#include "mountd.h"

/* mount(2) needs filesystem type to mount the device, so we got to
   figure that out by reading the data on the device. This would better
   be avoided since mountd is a highly-privileged process, but it does
   not look like there's a better way right now.
 
   Here we also check whether the user should be allowed to mount
   given device in the first place. The name comes directly from
   the user and could be anything. */

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

static int test_vfat(int fd)
{
	char buf[512];
	char magic[] = { 0x55, 0xAA };
	char fat16[] = "FAT16";
	char fat32[] = "FAT32";
	int ret;

	if((ret = read_at_off(fd, 0, buf, sizeof(buf))) < 0)
		return ret;
	if(memcmp(buf + 510, magic, sizeof(magic)))
		return -1;

	if(!memcmp(buf + 82, fat32, 5))
		return 0;
	if(!memcmp(buf + 54, fat16, 5))
		return 0;

	return -1;
}

static const struct fstest {
	int type;
	char name[8];
	int (*check)(int fd);
} tests[] = {
	{ FS_EXT4,    "ext4",    test_ext_fs  },
	{ FS_VFAT,    "vfat",    test_vfat    },
	{ FS_ISO9660, "iso9660", test_iso9660 }
};

static int guess_fs_type(int fd)
{
	const struct fstest* ft;

	for(ft = tests; ft < tests + ARRAY_SIZE(tests); ft++)
		if(ft->check(fd) == 0)
			return ft->type;

	return -ENODATA;
}

const char* fs_type_string(int fst)
{
	const struct fstest* ft;

	for(ft = tests; ft < tests + ARRAY_SIZE(tests); ft++)
		if(ft->type == fst)
			return ft->name;

	return "unknown";
}

/* Sticky bit is used to protect system devices; mountd will not touch
   anything with the bit set. It's up to the boot tools to set it.
 
   The original pmount tries to check for removable flag in sysfs instead,
   but that turns out to be very unreliable. MMC devices may be marked
   non-removable when they are and removable when they aren't. */

static int isblock(int fd)
{
	int ret;
	struct stat st;

	if((ret = sys_fstat(fd, &st)) < 0)
		return ret;
	if((st.mode & S_IFMT) != S_IFBLK)
		return -ENOTBLK;
	if((st.mode & S_ISVTX))
		return -EPERM;

	if(!(st.mode & 0777))
		return -EBUSY; /* mounted */
	if(st.uid)
		return -EBUSY; /* grabbed */

	return 0;
}

int check_blkdev(char* name, char* path, int isloop)
{
	int fd, ret;

	if((fd = sys_open(path, O_RDONLY)) < 0)
		return fd;

	if(!isloop && (ret = isblock(fd)) < 0)
		return ret;

	ret = guess_fs_type(fd);

	sys_close(fd);

	return ret;
}

int grab_blkdev(char* path, struct ucred* uc)
{
	int fd, ret;

	if((fd = sys_open(path, O_RDONLY)) < 0)
		return fd;
	if((ret = isblock(fd)) < 0)
		return ret;

	ret = sys_fchown(fd, uc->uid, 0);

	sys_close(fd);

	return ret;
}

int release_blkdev(char* path, struct ucred* uc)
{
	int fd, ret;
	struct stat st;

	if((fd = sys_open(path, O_RDONLY)) < 0)
		return fd;
	if((ret = sys_fstat(fd, &st)) < 0)
		return ret;
	if(st.uid != uc->uid)
		return -EPERM;

	ret = sys_fchown(fd, 0, 0);

	sys_close(fd);

	return ret;
}

/* There's also there data field in mount(2) call. Some fs-es (FAT)
   need non-empty string here just to work properly. Others don't.

   The user may also want to supply some options, which then have to
   be checked, but this is not allowed yet. */

static char* prep_vfat_opts(char* p, char* e, struct ucred* uc)
{
	p = fmtstr(p, e, "discard");

	p = fmtstr(p, e, ",");
	p = fmtstr(p, e, "uid=");
	p = fmtint(p, e, uc->uid);

	p = fmtstr(p, e, ",");
	p = fmtstr(p, e, "gid=");
	p = fmtint(p, e, uc->gid);

	p = fmtstr(p, e, ",umask=0117");
	p = fmtstr(p, e, ",dmask=0006");
	p = fmtstr(p, e, ",tz=UTC");

	return p;
}

int prep_fs_options(char* buf, int len, int fstype, struct ucred* uc)
{
	char* p = buf;
	char* e = buf + len - 1;

	if(fstype == FS_VFAT)
		p = prep_vfat_opts(p, e, uc);

	if(!p) return -1;

	*p = '\0';
	return 0;
}
