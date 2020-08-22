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

#include "passblk.h"

/* Once the keys are successfully unwrapped, we need to set up encryption
   layer over the specified partitions. The following is basically equivalent
   to running mainline dmsetup like this:

	dmsetup create $name
	dmsetup load $name --table crypto aes-xts-plain64 ... M:N ...

   with M:N being the major and minor numbers of the underlying encrypted
   device. See device-mapper documentations.

   DM ioctls are run on /dev/mapper/control, and we attemp to open that file
   very early, before starting the input code. This allows us to fail early,
   without asking for passphrase, in case DM itself is missing or badly
   misconfigured. */

void init_mapper(CTX)
{
	char* control = "/dev/mapper/control";
	int fd, ret;
	struct dm_ioctl dmi = {
		.version = { DM_VERSION_MAJOR, 0, 0 },
		.flags = 0,
		.data_size = sizeof(dmi)
	};

	if((fd = sys_open(control, O_RDONLY)) < 0)
		fail(NULL, control, fd);

	if((ret = sys_ioctl(fd, DM_VERSION, &dmi)) < 0)
		fail("ioctl", "DM_VERSION", ret);

	if(dmi.version[0] != DM_VERSION_MAJOR)
		fail("unsupported dm version", NULL, 0);

	ctx->mapfd = fd;
}

static void putstr(char* buf, char* src, int len)
{
	memcpy(buf, src, len);
	buf[len] = '\0';
}

static void dm_create(CTX, struct part* pt)
{
	char* label = pt->label;
	uint nlen = strlen(label);
	int fd = ctx->mapfd;
	int ret;

	struct dm_ioctl dmi = {
		.version = { DM_VERSION_MAJOR, 0, 0 },
		.flags = 0,
		.data_size = sizeof(dmi)
	};

	if(nlen > sizeof(dmi.name) - 1)
		fail(NULL, label, ENAMETOOLONG);

	putstr(dmi.name, label, nlen);

	if((ret = sys_ioctl(fd, DM_DEV_CREATE, &dmi)) < 0)
		fail("ioctl", "DM_DEV_CREATE", ret);

	pt->dmidx = minor(dmi.dev);
}

static void dm_single(CTX, struct part* pt, char* targ, char* opts)
{
	struct dm_ioctl* dmi;
	struct dm_target_spec* dts;
	char* optstring;

	char* name = pt->label;
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
	dts->length = pt->size / 512;
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

	if((ret = sys_ioctl(ctx->mapfd, DM_TABLE_LOAD, req)) < 0)
		fail("ioctl DM_TABLE_LOAD", name, ret);
}

static void dm_crypt(CTX, struct part* pt)
{
	int keyidx = pt->keyidx;
	int keyoffset = HDRSIZE + KEYSIZE*keyidx;
	void* key = ctx->keydata + keyoffset;
	int keylen = KEYSIZE;

	FMTBUF(p, e, buf, 256);
	p = fmtstr(p, e, "aes-xts-plain64");
	p = fmtstr(p, e, " ");
	p = fmtbytes(p, e, key, keylen);
	p = fmtstr(p, e, " 0 ");
	p = fmtlong(p, e, major(pt->rdev));
	p = fmtstr(p, e, ":");
	p = fmtlong(p, e, minor(pt->rdev));
	p = fmtstr(p, e, " 0 1 allow_discards");
	FMTEND(p, e);

	dm_single(ctx, pt, "crypt", buf);
}

static void dm_resume(CTX, struct part* pt)
{
	char* name = pt->label;
	int nlen = strlen(name);
	int ret;

	struct dm_ioctl dmi = {
		.version = { DM_VERSION_MAJOR, 0, 0 },
		.flags = DM_EXISTS_FLAG,
		.data_size = sizeof(dmi)
	};

	if(nlen > ssizeof(dmi.name) - 1)
		fail(NULL, name, -ENAMETOOLONG);

	putstr(dmi.name, name, nlen);

	if((ret = sys_ioctl(ctx->mapfd, DM_DEV_SUSPEND, &dmi)) < 0)
		fail("ioctl DM_DEV_SUSPEND", name, ret);
}

static void redo_symlink(CTX, struct part* pt)
{
	int ret;

	FMTBUF(lp, le, link, 200);
	lp = fmtstr(lp, le, "/dev/mapper/");
	lp = fmtstr(lp, le, pt->label);
	FMTEND(lp, le);

	FMTBUF(tp, te, targ, 200);
	tp = fmtstr(tp, te, "../dm-");
	tp = fmtint(tp, te, pt->dmidx);
	FMTEND(tp, te);

	(void)sys_unlink(link);

	if((ret = sys_symlink(targ, link)) < 0)
		fail(NULL, link, ret);
}

void decrypt_parts(CTX)
{
	int i, n = ctx->nparts;

	for(i = 0; i < n; i++) {
		struct part* pt = &ctx->parts[i];

		dm_create(ctx, pt);
		dm_crypt(ctx, pt);
		dm_resume(ctx, pt);

		redo_symlink(ctx, pt);
	}
}
