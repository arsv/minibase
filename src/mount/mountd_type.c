#include <sys/file.h>

#include <nlusctl.h>
#include <string.h>
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

static const struct fstest {
	int type;
	char name[8];
	int (*check)(int fd);
} tests[] = {
	{ FS_EXT4,    "ext4",    test_ext_fs  },
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

static int isblock(int fd)
{
	int ret;
	struct stat st;

	if((ret = sys_fstat(fd, &st)) < 0)
		return ret;
	if((st.mode & S_IFMT) != S_IFBLK)
		return -ENOTBLK;

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

/* There's also there data field in mount(2) call. Some fs-es (FAT)
   need non-empty string here just to work properly. Others don't.

   The user may also want to supply some options, which then have to
   be checked, but this is not allowed yet. */

int prep_fs_options(char* buf, int len, int fstype, struct ucbuf* uc)
{
	*buf = '\0';
	return 0;
}
