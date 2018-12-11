#include <sys/file.h>
#include <sys/fpath.h>

#include <nlusctl.h>
#include <format.h>
#include <string.h>
#include <printf.h>
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

static const char cfgname[] = HERE "/var/interfaces";
static const int maxsize = MAX_CFG_ENTRIES*sizeof(struct cfgent);

static void identify_usb(CTX, int fd)
{
	(void)fd; /* how tf do we identify USB devices? */
}

static void identify_sysdir(CTX, int fd)
{
	char device[100];
	char subsys[100];
	int dlen = sizeof(device) - 1;
	int slen = sizeof(subsys) - 1;

	if((dlen = sys_readlinkat(fd, "device", device, dlen)) < 0)
		return;
	if((slen = sys_readlinkat(fd, "device/subsystem", subsys, slen)) < 0)
		return;

	device[dlen] = '\0';
	subsys[slen] = '\0';

	char* devbase = basename(device);
	char* subbase = basename(subsys);

	if(!strcmp(subbase, "usb"))
		return identify_usb(ctx, fd);

	char* p = ctx->devid;
	char* e = p + sizeof(ctx->devid) - 1;

	p = fmtstr(p, e, subbase);
	p = fmtstr(p, e, "-");
	p = fmtstr(p, e, devbase);
	*p = '\0';
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
