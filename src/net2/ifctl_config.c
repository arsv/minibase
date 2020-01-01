#include <sys/file.h>
#include <sys/fpath.h>

#include <nlusctl.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "common.h"
#include "ifctl.h"

#define MAX_CFG_ENTRIES 64

struct cfgent {
	char mode[MODESIZE];
	char id[DEVIDLEN];
	char stop;
	char endl;
};

struct config {
	uint len;
	uint cap;
	void* buf;
};

#define CFG struct config* cfg

static const char cfgname[] = BASE_VAR "/interfaces";
static const int maxsize = MAX_CFG_ENTRIES*sizeof(struct cfgent);

/* USB auxiliary routines */

static int read_attr(int at, const char* name, char* buf, int len)
{
	int fd, rd;

	if((fd = sys_openat(at, name, O_RDONLY)) < 0)
		return fd;
	if((rd = sys_read(fd, buf, len - 1)) < 0)
		return rd;
	if(rd == 0)
		return rd;

	if(buf[rd-1] == '\n')
		rd--; /* trim EOL */
	else
		return 0; /* too long */

	buf[rd] = '\0';

	return rd;
}

static char* fmt_suffix(char* p, char* e, char* serial)
{
	long slen = strlen(serial);

	if(!*serial)
		return p;

	p = fmtstr(p, e, "-");

	long left = e - p;
	long over = slen - left;

	if(over > 0)
		serial += over;

	p = fmtstr(p, e, serial);

	return p;
}

/* USB has no fixed topology, but all devices report product
   and vendor ids, and some also have unique serials.

   /sus/class/net/wlan1/device -> ..../usb1/1-9/1-9:1.0/net/wlan1
   ..../usb1/1-9/1-9:1.0/net/wlan1/device -> ../../../1-9:1.0

   Now the tricky part: networking device is always an endpoint,
   not the whole USB device. Product and vendor ids we're after
   are attributes of the whole device. To get to the whole device,
   we need to take dirname(../../../1-9:1.0) = ../../..

   For now, the id algorithm assumes there's only one networking
   endpoint on any given USB device, and does not include endpoint
   in the id. The is not strictly speaking correct per USB specs,
   but perfectly fine in practice. */

static void use_usb_attrs(CTX, int at)
{
	char path[100];
	int plen = sizeof(path) - 1;
	int bt;

	if((plen = sys_readlinkat(at, "device", path, plen)) < 0)
		return;

	path[plen] = '\0';
	*(basename(path)) = '\0';

	if((bt = sys_openat(at, path, O_DIRECTORY)) < 0)
		return;

	char prod[10];
	char vend[10];
	char serial[40];

	if(read_attr(bt, "idProduct", prod, sizeof(prod)) <= 0)
		goto out;
	if(read_attr(bt, "idVendor", vend, sizeof(vend)) <= 0)
		goto out;
	if(read_attr(bt, "serial", serial, sizeof(serial)) <= 0)
		serial[0] = '\0';

	char* p = ctx->devid;
	char* e = p + sizeof(ctx->devid) - 1;

	p = fmtstr(p, e, "usb-");
	p = fmtstr(p, e, vend);
	p = fmtstr(p, e, ":");
	p = fmtstr(p, e, prod);
	p = fmt_suffix(p, e, serial);
	*p = '\0';
out:
	sys_close(bt);
}

/* For devices sitting on fixed-topology buses (USB, SDIO etc),
   bus address can be used as the id.

   /sys/class/net/wlan0/device -> ../../../0000:24:00.0

   Basename of the link targer is the bus address. */

static void use_bus_path(CTX, int at, char* bus)
{
	char device[100];
	int dlen = sizeof(device) - 1;

	if((dlen = sys_readlinkat(at, "device", device, dlen)) < 0)
		return;

	device[dlen] = '\0';

	char* devbase = basename(device);

	char* p = ctx->devid;
	char* e = p + sizeof(ctx->devid) - 1;

	p = fmtstr(p, e, bus);
	p = fmtstr(p, e, "-");
	p = fmtstr(p, e, devbase);
	*p = '\0';
}

static void identify_sysdir(CTX, int fd)
{
	char subsys[100];
	int slen = sizeof(subsys) - 1;

	if((slen = sys_readlinkat(fd, "device/subsystem", subsys, slen)) < 0)
		return;

	subsys[slen] = '\0';

	char* subbase = basename(subsys);

	if(!strcmp(subbase, "usb"))
		return use_usb_attrs(ctx, fd);

	return use_bus_path(ctx, fd, subbase);
}

void identify_device(CTX)
{
	int fd;

	memzero(ctx->devid, sizeof(ctx->devid));

	FMTBUF(p, e, dir, 100);
	p = fmtstr(p, e, "/sys/class/net/");
	p = fmtstr(p, e, ctx->name);
	FMTEND(p, e);

	if((fd = sys_open(dir, O_DIRECTORY)) < 0)
		return;

	identify_sysdir(ctx, fd);

	sys_close(fd);
}

static int read_config(struct config* cfg, char* buf, int maxsize)
{
	int fd, ret;
	struct stat st;

	memzero(cfg, sizeof(*cfg));

	cfg->buf = buf;
	cfg->cap = maxsize;

	if((fd = sys_open(cfgname, O_RDONLY)) < 0)
		return fd;
	if((ret = sys_fstat(fd, &st)) < 0)
		goto out;
	if(st.size > maxsize) {
		ret = -E2BIG;
		goto out;
	}

	int size = (int)st.size;

	if((ret = sys_read(fd, buf, size)) < 0)
		goto out;
	if(ret != size) {
		ret = -EINTR;
		goto out;
	}

	cfg->len = size;
out:
	sys_close(fd);

	return ret;
}

static int write_config(struct config* cfg)
{
	int fd, ret;
	int flags = O_WRONLY | O_TRUNC | O_CREAT;
	int mode = 0660;

	char* buf = cfg->buf;
	int len = cfg->len;

	if(!len) {
		sys_unlink(cfgname);
		return 0;
	}

	if((fd = sys_open3(cfgname, flags, mode)) < 0)
		return fd;

	if((ret = sys_write(fd, buf, len)) < 0)
		goto out;
	if(ret != len)
		ret = -EINTR;
out:
	sys_close(fd);

	return ret;
}

static struct cfgent* config_entry(CFG, void* at)
{
	void* buf = cfg->buf;
	void* end = buf + cfg->len;

	if(at < buf)
		return NULL;
	if(at + sizeof(struct cfgent) > end)
		return NULL;

	return (struct cfgent*)at;
}

static struct cfgent* first_entry(CFG)
{
	return config_entry(cfg, cfg->buf);
}

static struct cfgent* next_entry(CFG, struct cfgent* ce)
{
	return config_entry(cfg, ((void*)ce) + sizeof(*ce));
}

static void clear_entries(CFG, char* mode)
{
	struct cfgent* ce;

	uint len = strlen(mode);

	if(len > sizeof(ce->mode) - 1)
		return;

	for(ce = first_entry(cfg); ce; ce = next_entry(cfg, ce))
		if(!strncmp(ce->mode, mode, sizeof(ce->mode)))
			memzero(ce, sizeof(*ce));
}

static struct cfgent* find_entry(CFG, CTX)
{
	struct cfgent* ce;

	for(ce = first_entry(cfg); ce; ce = next_entry(cfg, ce))
		if(!memcmp(ce->id, ctx->devid, DEVIDLEN))
			return ce;

	return NULL;
}

static struct cfgent* create_entry(CFG)
{
	struct cfgent* ce;

	if(cfg->len + sizeof(*ce) > cfg->cap)
		return NULL;

	ce = cfg->buf + cfg->len;
	cfg->len += sizeof(*ce);

	ce->stop = '\0';
	ce->endl = '\n';

	return ce;
}

static void set_device_mode(CFG, CTX, char* mode)
{
	struct cfgent* ce;

	uint len = strlen(mode);

	if(len > sizeof(ce->mode) - 1)
		return;

	if((ce = find_entry(cfg, ctx))) {
		memcpy(ce->mode, mode, len + 1);
	} else if((ce = create_entry(cfg))) {
		memcpy(ce->mode, mode, len + 1);
		memcpy(ce->id, ctx->devid, DEVIDLEN);
	}
}

static void delete_entry(CFG, struct cfgent* ce)
{
	struct cfgent* cf = first_entry(cfg);
	struct cfgent* cn = next_entry(cfg, ce);

	if(cn) { /* non-last entry, move tail over */
		int ts = (int)(cn - cf); /* tail start */
		int tl = cfg->len - ts;   /* tail length */
		int hl = (int)(ce - cf); /* head length */
		memmove(ce, cn, tl);
		cfg->len = hl + tl;
	} else { /* truncate */
		cfg->len = (int)(ce - cf);
	}
}

void store_device_mode(CTX, char* mode)
{
	char data[maxsize];
	struct config cfg;

	if(!ctx->devid[0]) /* not identified */
		return;

	(void)read_config(&cfg, data, sizeof(data));

	clear_entries(&cfg, mode);

	set_device_mode(&cfg, ctx, mode);

	write_config(&cfg);
}

void store_device_also(CTX, char* mode)
{
	char data[maxsize];
	struct config cfg;

	if(!ctx->devid[0]) /* not identified */
		return;

	(void)read_config(&cfg, data, sizeof(data));

	set_device_mode(&cfg, ctx, mode);

	write_config(&cfg);
}

void clear_device_entry(CTX)
{
	char data[maxsize];
	struct config cfg;
	struct cfgent* ce;

	if(!ctx->devid[0]) /* not identified */
		return;
	if(read_config(&cfg, data, sizeof(data)) < 0)
		return;
	if(!(ce = find_entry(&cfg, ctx)))
		return;

	delete_entry(&cfg, ce);

	write_config(&cfg);
}

void load_device_mode(CTX, char* mode, int size)
{
	char data[maxsize];
	struct config cfg;
	struct cfgent* ce;

	if(!ctx->devid[0]) /* not identified */
		return;
	if(read_config(&cfg, data, sizeof(data)) < 0)
		return;
	if(!(ce = find_entry(&cfg, ctx)))
		return;

	int len = strnlen(ce->mode, sizeof(ce->mode));

	if(len >= size - 1)
		return;

	memcpy(mode, ce->mode, len + 1);
}
