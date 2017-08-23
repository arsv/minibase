#include <bits/ioctl/block.h>
#include <bits/ioctl/mapper.h>
#include <bits/major.h>

#include <sys/file.h>
#include <sys/ioctl.h>

#include <format.h>
#include <string.h>
#include <fail.h>

ERRTAG = "create";
ERRLIST = {
	REPORT(EINVAL), REPORT(EPERM), REPORT(EACCES), REPORT(EBUSY),
	RESTASNUMBERS
};

struct device {
	uint64_t rdev;
	uint64_t mdev;
	uint64_t size;
};

static int error(const char* msg, const char* arg, int err)
{
	warn(msg, arg, err);
	return err ? err : -EINVAL;
}

static void prep_device(struct device* dev, const char* name)
{
	int fd, ret;
	struct stat st;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail("open", name, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", name, ret);
	if((ret = sys_ioctl(fd, BLKGETSIZE64, &(dev->size))))
		fail("ioctl BLKGETSIZE64", name, ret);

	dev->rdev = st.rdev;
	dev->mdev = 0;
}

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

static void dm_create(int fd, char* name, struct device* dev)
{
	int nlen = strlen(name);
	int ret;

	struct dm_ioctl dmi = {
		.version = { DM_VERSION_MAJOR, 0, 0 },
		.flags = 0,
		.data_size = sizeof(dmi)
	};

	if(nlen > sizeof(dmi.name) - 1)
		fail(NULL, name, ENAMETOOLONG);

	putstr(dmi.name, name, nlen);

	if((ret = sys_ioctl(fd, DM_DEV_CREATE, &dmi)) < 0)
		fail("ioctl", "DM_DEV_CREATE", ret);

	dev->rdev = dmi.dev;
}

static int dm_single(int fd, char* name, uint64_t size, char* targ, char* opts)
{
	struct dm_ioctl* dmi;
	struct dm_target_spec* dts;
	char* optstring;

	int nlen = strlen(name);
	int tlen = strlen(targ);
	int olen = strlen(opts);
	int dmilen = sizeof(*dmi);
	int dtslen = sizeof(*dts);

	int reqlen = dmilen + dtslen + olen + 1;
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
		return error(NULL, name, -ENAMETOOLONG);
	if(tlen > sizeof(dts->type) - 1)
		return error(NULL, targ, -ENAMETOOLONG);

	putstr(dmi->name, name, nlen);
	putstr(dts->type, targ, tlen);
	putstr(optstring, opts, olen);

	int ret;

	if((ret = sys_ioctl(fd, DM_TABLE_LOAD, req)) < 0)
		warn("ioctl", "DM_TABLE_LOAD", ret);

	return ret;
}

static int dm_crypto(int fd, char* name, struct device* dev,
                      char* cipher, void* key, int keylen)
{
	int ciphlen = strlen(cipher);
	char buf[ciphlen + 2*keylen + 50];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = fmtstr(p, e, cipher);
	p = fmtstr(p, e, " ");
	p = fmtbytes(p, e, key, keylen);
	p = fmtstr(p, e, " 0 ");
	p = fmtlong(p, e, major(dev->rdev));
	p = fmtstr(p, e, ":");
	p = fmtlong(p, e, minor(dev->rdev));
	p = fmtstr(p, e, " 0 1 allow_discards");
	*p++ = '\0';

	return dm_single(fd, name, dev->size, "crypt", buf);
}

static int dm_suspend(int fd, char* name, int flags)
{
	int nlen = strlen(name);
	int ret;

	struct dm_ioctl dmi = {
		.version = { DM_VERSION_MAJOR, 0, 0 },
		.flags = flags,
		.data_size = sizeof(dmi)
	};

	if(nlen > sizeof(dmi.name) - 1)
		return error(NULL, name, -ENAMETOOLONG);

	putstr(dmi.name, name, nlen);

	if((ret = sys_ioctl(fd, DM_DEV_SUSPEND, &dmi)) < 0)
		warn("ioctl", "DM_DEV_SUSPEND", ret);

	return ret;
}

static void dm_remove(int fd, char* name)
{
	int nlen = strlen(name);
	int ret;

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

int main(void)
{
	int fd, ret;

	char* base = "/dev/loop0";
	char* name = "loop0";
	struct device dev;
	char* cipher = "aes-xts-plain64";
	uint8_t key[64] = {
		0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0,
		0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0,
		0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0,
		0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0
	};
	int keylen = sizeof(key);

	prep_device(&dev, base);
	
	fd = dm_open_control();
	dm_create(fd, name, &dev);

	if((ret = dm_crypto(fd, name, &dev, cipher, key, keylen)))
		goto out;
	if((ret = dm_suspend(fd, name, 0)))
		goto out;
out:
	if(ret < 0)
		dm_remove(fd, name);

	return ret ? -1 : 0;
}
