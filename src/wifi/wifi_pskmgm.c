#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/fprop.h>

#include <crypto/pbkdf2.h>
#include <nlusctl.h>
#include <string.h>
#include <output.h>
#include <format.h>
#include <heap.h>
#include <util.h>

#include "common.h"
#include "wifi.h"

#define MAX_PSK_ENTRIES 64

static const char cfgname[] = HERE "/var/wifi-psk";

struct saved {
	byte ssid[32];
	byte psk[32];
	byte type;
	byte slen;
	byte _pad;
	byte nl;
};

struct config {
	int fd;
	uint len;
	void* buf;
};

#define CFG struct config* cfg

static int input_passphrase(char* buf, int len)
{
	int rd;
	char* prompt = "Passphrase: ";

	sys_write(STDOUT, prompt, strlen(prompt));
	rd = sys_read(STDIN, buf, len);

	if(rd >= len)
		fail("passphrase too long", NULL, 0);

	if(rd > 0 && buf[rd-1] == '\n')
		rd--;
	if(!rd)
		fail("empty passphrase rejected", NULL, 0);

	buf[rd] = '\0';

	return rd;
}

void ask_passphrase(CTX)
{
	byte* ssid = ctx->ssid;
	uint slen = ctx->slen;

	char pass[100];
	int plen;

	plen = input_passphrase(pass, sizeof(pass));

	pbkdf2_sha1(ctx->psk, sizeof(ctx->psk), pass, plen, ssid, slen, 4096);

	ctx->unsaved = 1;
}

/* PSK database routines */

static void open_read_config(CTX, CFG, int mode)
{
	int fd, ret;
	struct stat st;

	memzero(cfg, sizeof(*cfg));

	if((fd = sys_open(cfgname, mode)) < 0) {
		if(fd != -ENOENT && fd != -ENOTDIR)
			fail(NULL, cfgname, fd);
		cfg->fd = -1;
		return;
	}

	if((ret = sys_fstat(fd, &st)) < 0)
		fail(NULL, cfgname, ret);
	if(st.size > (int)(MAX_PSK_ENTRIES*sizeof(struct saved)))
		fail(NULL, cfgname, -E2BIG);
	if(st.size % sizeof(struct saved))
		fail(NULL, cfgname, -EINVAL);

	int size = st.size;
	int rd;
	void* buf = heap_alloc(ctx, size);

	if((rd = sys_read(fd, buf, size)) < 0)
		fail("read", cfgname, rd);
	if(rd != size)
		fail("incomplete read in", cfgname, 0);

	cfg->fd = fd;
	cfg->buf = buf;
	cfg->len = size;
}

static void read_config(CTX, CFG)
{
	open_read_config(ctx, cfg, O_RDONLY);
}

static void open_config(CTX, CFG)
{
	open_read_config(ctx, cfg, O_RDWR);
}

static void drop_config(CTX, CFG)
{
	if(cfg->fd < 0)
		return;

	sys_close(cfg->fd);
	ctx->ptr = cfg->buf;

	memzero(cfg, sizeof(*cfg));

	cfg->fd = -1;
}

static void save_config(CTX, CFG)
{
	int fd = cfg->fd;
	int len = cfg->len;
	void* buf = cfg->buf;
	int ret;

	if(fd < 0)
		return;

	if(!len) {
		if((ret = sys_unlink(cfgname)) < 0)
			fail(NULL, cfgname, ret);
		return;
	}

	if((ret = sys_ftruncate(fd, 0)) < 0)
		fail(NULL, cfgname, ret);
	if((ret = sys_write(fd, buf, len)) < 0)
		fail(NULL, cfgname, ret);
	if(ret != len)
		fail("incomplete write in", cfgname, 0);

	drop_config(ctx, cfg);
}

static void remove_entry(CFG, struct saved* sv)
{
	void* ss = sv;
	void* se = ss + sizeof(*sv);

	void* buf = cfg->buf;
	int len = cfg->len;

	if(!buf)
		return;
	if(ss < buf)
		return;

	int left = buf + len - se;

	if(left < 0)
		return;
	if(left > 0)
		memmove(ss, se, left);

	cfg->len -= sizeof(*sv);
}

static void append_entry(CFG, struct saved* sv)
{
	int fd, ret;
	int flags = O_WRONLY | O_CREAT | O_EXCL;
	int mode = 0600;

	if((fd = cfg->fd) >= 0) {
		if((ret = sys_seek(fd, cfg->len)) < 0)
			fail("seek", cfgname, ret);
	} else {
		if((fd = sys_open3(cfgname, flags, mode)) < 0)
			fail(NULL, cfgname, fd);
	}

	void* buf = sv;
	int len = sizeof(*sv);

	if((ret = sys_write(fd, buf, len)) < 0)
		fail("write", cfgname, ret);
	if(ret != len)
		fail("incomplete write in", cfgname, 0);
}

static struct saved* first(CFG)
{
	struct saved* sv = cfg->buf;

	if(!cfg->buf)
		return NULL;
	if(cfg->len < sizeof(*sv))
		return NULL;

	return sv;
}

static struct saved* next(CFG, struct saved* sv)
{
	void* s0 = sv;
	void* s1 = s0 + sizeof(*sv);
	void* s2 = s1 + sizeof(*sv);

	void* buf = cfg->buf;
	void* end = buf + cfg->len;

	if(s0 < buf)
		return NULL;
	if(s2 > end)
		return NULL;

	return s1;
}

static struct saved* find_entry(CTX, CFG)
{
	struct saved* sv;

	for(sv = first(cfg); sv; sv = next(cfg, sv))
		if(!memcmp(ctx->ssid, sv->ssid, 32))
			return sv;

	return NULL;
}

int load_saved_psk(CTX)
{
	struct config cfg;
	struct saved* sv;

	read_config(ctx, &cfg);

	if((sv = find_entry(ctx, &cfg)))
		memcpy(ctx->psk, sv->psk, 32);

	drop_config(ctx, &cfg);

	return !!sv;
}

void maybe_store_psk(CTX)
{
	struct config cfg;
	struct saved* old;

	if(!ctx->unsaved)
		return;

	open_config(ctx, &cfg);

	if((old = find_entry(ctx, &cfg))) {
		memcpy(old->psk, ctx->psk, 32);
		save_config(ctx, &cfg);
		return;
	}

	struct saved new;

	memzero(&new, sizeof(new));
	memcpy(new.ssid, ctx->ssid, 32);
	memcpy(new.psk, ctx->psk, 32);
	new.slen = ctx->slen;
	new.type = 0;
	new.nl = '\n';

	append_entry(&cfg, &new);
	drop_config(ctx, &cfg);
}

void remove_psk_entry(CTX)
{
	struct config cfg;
	struct saved* sv;

	open_config(ctx, &cfg);

	if(!(sv = find_entry(ctx, &cfg)))
		fail("no saved PSK for this SSID", NULL, 0);

	remove_entry(&cfg, sv);
	save_config(ctx, &cfg);
}

void list_saved_psks(CTX)
{
	struct config cfg;
	struct saved* sv;

	char out[1024];
	struct bufout bo = {
		.fd = STDOUT,
		.len = sizeof(out),
		.ptr = 0,
		.buf = out
	};

	read_config(ctx, &cfg);

	if(!cfg.buf || !cfg.len)
		fail("no saved PSKs", NULL, 0);

	for(sv = first(&cfg); sv; sv = next(&cfg, sv)) {
		FMTBUF(p, e, buf, 100);
		p = fmt_ssid(p, e, sv->ssid, sv->slen);
		FMTENL(p, e);

		bufout(&bo, buf, p - buf);
	}

	bufoutflush(&bo);
}
