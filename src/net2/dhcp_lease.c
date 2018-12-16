#include <sys/file.h>
#include <sys/fpath.h>
#include <format.h>
#include <printf.h>
#include <string.h>
#include <util.h>
#include <dirs.h>

#include "dhcp.h"

#define LEASEDIR HERE "/var/dhcp"

static void make_device_name(char* buf, int len)
{
	char* p = buf;
	char* e = buf + len - 1;

	p = fmtstr(p, e, LEASEDIR "/");
	p = fmtstr(p, e, device);
	*p = '\0';

	/* Sanity check, should never get triggered.
	   Overly long device names should get caught much earlier. */
	if(p >= e)
		fail("device name too long:", device, 0);
}

void delete_lease(void)
{
	char path[100];
	int ret;

	make_device_name(path, sizeof(path));

	if((ret = sys_unlink(path)) < 0)
		fail(NULL, path, ret);
}

void save_lease(void)
{
	char path[100];
	int fd, ret;
	int flags = O_WRONLY | O_CREAT | O_TRUNC;
	int mode = 0664;

	FMTBUF(p, e, buf, sizeof(offer) + optptr);
	p = fmtraw(p, e, &offer, sizeof(offer));
	p = fmtraw(p, e, packet.options, optptr);

	make_device_name(path, sizeof(path));

	if((ret = sys_mkdir(LEASEDIR, 0755)) >= 0)
		;
	else if(ret != -EEXIST)
		fail(NULL, LEASEDIR, ret);

	if((fd = sys_open3(path, flags, mode)) < 0)
		fail(NULL, path, fd);

	if((ret = sys_write(fd, buf, p - buf)) < 0)
		warn(NULL, path, ret);

	sys_close(fd);
}

void load_lease(void)
{
	char path[100];
	int max = sizeof(offer) + sizeof(packet.options);
	char buf[1024];
	int fd, ret;

	make_device_name(path, sizeof(path));

	if((fd = sys_open(path, O_RDONLY)) < 0)
		fail(NULL, path, fd);
	if((ret = sys_read(fd, buf, sizeof(buf))) < 0)
		;
	else if(ret > max)
		ret = -E2BIG;
	else if(ret < (int)sizeof(offer))
		ret = -EINVAL;

	sys_close(fd);

	if(ret < 0)
		fail(NULL, path, ret);

	memzero(&packet, sizeof(packet));
	memcpy(&offer, buf, sizeof(offer));
	memcpy(&packet.options, buf + sizeof(offer), ret - sizeof(offer));
	optptr = ret - sizeof(offer);
}

void check_no_lease(void)
{
	char path[100];
	struct stat st;
	int ret;

	make_device_name(path, sizeof(path));

	if((ret = sys_stat(path, &st)) >= 0)
		fail(NULL, path, -EEXIST);
}
