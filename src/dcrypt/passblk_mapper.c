#include <bits/ioctl/mapper.h>
#include <bits/ioctl/block.h>
#include <bits/major.h>

#include <sys/fpath.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/mman.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "common.h"
#include "passblk.h"

int dmfd;

void open_dm_control(void)
{
	char* control = "/dev/mapper/control";
	int fd, ret;
	struct dm_ioctl dmi = {
		.version = { DM_VERSION_MAJOR, 0, 0 },
		.flags = 0,
		.data_size = sizeof(dmi)
	};

	if((fd = sys_open(control, O_RDONLY)) < 0)
		quit(NULL, control, fd);

	if((ret = sys_ioctl(fd, DM_VERSION, &dmi)) < 0)
		quit("ioctl", "DM_VERSION", ret);

	if(dmi.version[0] != DM_VERSION_MAJOR)
		quit("unsupported dm version", NULL, 0);

	dmfd = fd;
}

static void putstr(char* buf, char* src, int len)
{
	memcpy(buf, src, len);
	buf[len] = '\0';
}

static void dm_create(char* name)
{
	int nlen = strlen(name);
	int ret;

	struct dm_ioctl dmi = {
		.version = { DM_VERSION_MAJOR, 0, 0 },
		.flags = 0,
		.data_size = sizeof(dmi)
	};

	if(nlen > sizeof(dmi.name) - 1)
		quit(NULL, name, ENAMETOOLONG);

	putstr(dmi.name, name, nlen);

	if((ret = sys_ioctl(dmfd, DM_DEV_CREATE, &dmi)) < 0)
		quit("ioctl", "DM_DEV_CREATE", ret);
}

static int dm_single(char* name, uint64_t size, char* targ, char* opts)
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

	memzero(dmi, sizeof(*dmi));
	dmi->version[0] = DM_VERSION_MAJOR;
	dmi->version[1] = 0;
	dmi->version[2] = 0;
	dmi->flags = 0;
	dmi->data_size = sizeof(req);
	dmi->data_start = dmilen;
	dmi->target_count = 1;

	memzero(dts, sizeof(*dts));
	dts->start = 0;
	dts->length = size / 512;
	dts->status = 0;
	dts->next = 0;

	if(nlen > sizeof(dmi->name) - 1)
		quit(NULL, name, -ENAMETOOLONG);
	if(tlen > sizeof(dts->type) - 1)
		quit(NULL, targ, -ENAMETOOLONG);

	putstr(dmi->name, name, nlen);
	putstr(dts->type, targ, tlen);
	putstr(optstring, opts, olen);

	int ret;

	if((ret = sys_ioctl(dmfd, DM_TABLE_LOAD, req)) < 0)
		quit("ioctl DM_TABLE_LOAD", name, ret);

	return ret;
}

static int dm_crypt(struct part* pt, char* cipher, void* key, int keylen)
{
	FMTBUF(p, e, buf, strlen(cipher) + 2*keylen + 50);
	p = fmtstr(p, e, cipher);
	p = fmtstr(p, e, " ");
	p = fmtbytes(p, e, key, keylen);
	p = fmtstr(p, e, " 0 ");
	p = fmtlong(p, e, major(pt->rdev));
	p = fmtstr(p, e, ":");
	p = fmtlong(p, e, minor(pt->rdev));
	p = fmtstr(p, e, " 0 1 allow_discards");
	FMTEND(p, e);

	return dm_single(pt->label, pt->size, "crypt", buf);
}

static int dm_suspend(char* name, int flags)
{
	int nlen = strlen(name);
	int ret;

	struct dm_ioctl dmi = {
		.version = { DM_VERSION_MAJOR, 0, 0 },
		.flags = flags,
		.data_size = sizeof(dmi)
	};

	if(nlen > sizeof(dmi.name) - 1)
		quit(NULL, name, -ENAMETOOLONG);

	putstr(dmi.name, name, nlen);

	if((ret = sys_ioctl(dmfd, DM_DEV_SUSPEND, &dmi)) < 0) {
		//warn("ioctl", "DM_DEV_SUSPEND", ret);
	}

	return ret;
}

static void dm_remove(char* name)
{
	int nlen = strlen(name);
	int ret;

	struct dm_ioctl dmi = {
		.version = { DM_VERSION_MAJOR, 0, 0 },
		.flags = 0,
		.data_size = sizeof(dmi)
	};

	if(nlen > sizeof(dmi.name) - 1)
		quit(NULL, name, ENAMETOOLONG);

	putstr(dmi.name, name, nlen);

	if((ret = sys_ioctl(dmfd, DM_DEV_REMOVE, &dmi)) < 0)
		quit("ioctl", "DM_DEV_REMOVE", ret);
}

static int create_dm_crypt(struct part* pt)
{
	char* label = pt->label;
	void* key = get_key(pt->keyidx);
	int ret;

	char* cipher = "aes-xts-plain64";
	uint8_t xtskey[32];
	memcpy(xtskey + 0,  key, 16);
	memcpy(xtskey + 16, key, 16);
	int xtslen = 32;

	dm_create(label);

	if((ret = dm_crypt(pt, cipher, xtskey, xtslen)))
		goto out;
	if((ret = dm_suspend(label, 0)))
		goto out;
out:
	if(ret < 0)
		dm_remove(label);

	memzero(xtskey, sizeof(xtskey));

	return ret;
}

static void remove_dm_crypt(char* name)
{
	dm_single(name, 512, "error", "");
	dm_suspend(name, 0);
	dm_remove(name);
}

static int query_rdev_size(struct part* pt)
{
	int fd, ret;
	struct stat st;

	char* pref = "/dev/";
	char* name = pt->name;

	FMTBUF(p, e, path, strlen(pref) + strlen(name) + 4);
	p = fmtstr(p, e, pref);
	p = fmtstr(p, e, name);
	FMTEND(p, e);

	if((fd = sys_open(path, O_RDONLY)) < 0)
		quit("open", path, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		quit("stat", path, ret);
	if((ret = sys_ioctl(fd, BLKGETSIZE64, &(pt->size))))
		quit("ioctl BLKGETSIZE64", path, ret);

	pt->rdev = st.rdev;
	pt->fd = fd;

	return 0;
}

void query_part_inodes(void)
{
	struct part* pt;

	for(pt = parts; pt < parts + nparts; pt++)
		query_rdev_size(pt);
}

void setup_devices(void)
{
	struct part* pt;
	int ret = 0;

	for(pt = parts; pt < parts + nparts; pt++)
		if(!pt->keyidx)
			continue;
		else if((ret = create_dm_crypt(pt)) < 0)
			break;
	if(!ret)
		return;

	for(pt--; pt >= parts; pt--)
		if(pt->keyidx)
			remove_dm_crypt(pt->label);
}

static void link_part(char* name, char* label)
{
	FMTBUF(lp, le, link, 100);
	lp = fmtstr(lp, le, MAPDIR);
	lp = fmtstr(lp, le, "/");
	lp = fmtstr(lp, le, label);
	FMTEND(lp, le);

	FMTBUF(pp, pe, path, 100);
	pp = fmtstr(pp, pe, "/dev/");
	pp = fmtstr(pp, pe, name);
	FMTEND(pp, pe);

	sys_symlink(path, link);
}

void link_plain_partitions(void)
{
	struct part* pt;

	sys_mkdir(MAPDIR, 0755);

	for(pt = parts; pt < parts + nparts; pt++)
		if(!pt->keyidx)
			link_part(pt->name, pt->label);
}
