#include <bits/ioctl/mapper.h>
#include <sys/file.h>
#include <sys/ioctl.h>

#include <format.h>
#include <string.h>
#include <util.h>
#include <main.h>

ERRTAG("remove");
ERRLIST(NEINVAL NEPERM NEACCES NEBUSY NENODEV NENXIO);

static int dm_open_control(void)
{
	char* control = "/dev/mapper/control";
	int fd, ret;
	struct dm_ioctl dmi = {
		.version = { DM_VERSION_MAJOR, 0, 0 },
		.flags = 0,
		.data_size = sizeof(dmi)
	};

	if((fd = sys_open(control, O_RDONLY)) < 0)
		fail("open", control, fd);

	if((ret = sys_ioctl(fd, DM_VERSION, &dmi)) < 0)
		fail("ioctl", "DM_VERSION", ret);

	if(dmi.version[0] != DM_VERSION_MAJOR)
		fail("unsupported dm version", NULL, 0);

	return fd;
}

static void putstr(char* buf, char* src, int len)
{
	memcpy(buf, src, len);
	buf[len] = '\0';
}

static void dm_suspend(int fd, char* name, int flags)
{
	uint nlen = strlen(name);
	long ret;

	struct dm_ioctl dmi = {
		.version = { DM_VERSION_MAJOR, 0, 0 },
		.flags = flags,
		.data_size = sizeof(dmi)
	};

	if(nlen > sizeof(dmi.name) - 1)
		fail(NULL, name, -ENAMETOOLONG);

	putstr(dmi.name, name, nlen);

	if((ret = sys_ioctl(fd, DM_DEV_SUSPEND, &dmi)) < 0)
		fail("ioctl", "DM_DEV_SUSPEND", ret);
}

static int dm_single(int fd, char* name, uint64_t size, char* targ, char* opts)
{
	struct dm_ioctl* dmi;
	struct dm_target_spec* dts;
	char* optstring;

	uint nlen = strlen(name);
	uint tlen = strlen(targ);
	uint olen = strlen(opts);
	uint dmilen = sizeof(*dmi);
	uint dtslen = sizeof(*dts);

	uint reqlen = dmilen + dtslen + olen + 1;
	char req[reqlen];

	dmi = (struct dm_ioctl*)(req + 0);
	dts = (struct dm_target_spec*)(req + dmilen);
	optstring = (req + dmilen + dtslen);

	dmi->version[0] = DM_VERSION_MAJOR;
	dmi->version[1] = 0;
	dmi->version[2] = 0;
	dmi->flags = 0;
	dmi->data_size = sizeof(req);
	dmi->data_start = dmilen;
	dmi->target_count = 1;

	dts->start = 0;
	dts->length = size / 512;
	dts->status = 0;
	dts->next = 0;

	if(nlen > sizeof(dmi->name) - 1)
		fail(NULL, name, -ENAMETOOLONG);
	if(tlen > sizeof(dts->type) - 1)
		fail(NULL, targ, -ENAMETOOLONG);

	putstr(dmi->name, name, nlen);
	putstr(dts->type, targ, tlen);
	putstr(optstring, opts, olen);

	int ret;

	if((ret = sys_ioctl(fd, DM_TABLE_LOAD, req)) < 0)
		warn("ioctl", "DM_TABLE_LOAD", ret);

	return ret;
}

static void dm_remove(int fd, char* name)
{
	uint nlen = strlen(name);
	long ret;

	struct dm_ioctl dmi = {
		.version = { DM_VERSION_MAJOR, 0, 0 },
		.flags = 0,
		.data_size = sizeof(dmi)
	};

	if(nlen > sizeof(dmi.name) - 1)
		fail(NULL, name, ENAMETOOLONG);

	putstr(dmi.name, name, nlen);

	if((ret = sys_ioctl(fd, DM_DEV_REMOVE, &dmi)) < 0)
		fail("ioctl", "DM_DEV_REMOVE", ret);
}

int main(noargs)
{
	char* name = "loop0";
	int fd;

	fd = dm_open_control();
	dm_single(fd, name, 512, "error", "");
	dm_suspend(fd, name, 0);
	dm_remove(fd, name);

	return 0;
}
