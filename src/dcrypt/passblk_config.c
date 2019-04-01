#include <sys/file.h>
#include <sys/mman.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "common.h"
#include "passblk.h"

#define PAGE 4096

struct ctx {
	char* file;
	int line;
	int argc;
	char** args;
	int devidx;
};

struct bdev bdevs[NBDEVS];
struct part parts[NPARTS];

int nbdevs;
int nparts;

static char smallbuf[1024];

static void parse_error(struct ctx* ctx, const char* msg, char* arg)
{
	FMTBUF(p, e, buf, 100);

	p = fmtstr(p, e, ctx->file);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, ctx->line);
	p = fmtstr(p, e, ":");
	p = fmtstr(p, e, " ");

	p = fmtstr(p, e, msg);

	if(arg) {
		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, arg);
	}

	FMTENL(p, e);

	sys_write(STDERR, buf, p - buf);
	_exit(-1);
}

void copy_sized(struct ctx* ctx, char* buf, int blen, int i)
{
	if(i >= ctx->argc)
		return;

	char* val = ctx->args[i];
	int vlen = strlen(val);

	if(vlen > blen - 1)
		parse_error(ctx, "argument too long", NULL);

	memcpy(buf, val, vlen);
	buf[vlen] = '\0';
}

void parse_kidx(struct ctx* ctx, int* dst, int i)
{
	int v;
	char* p;

	if(i >= ctx->argc)
		return;
	if(!strcmp(ctx->args[i], "-"))
		return;

	if(!(p = parseint(ctx->args[i], &v)) || *p)
		parse_error(ctx, "integer value required", NULL);

	*dst = v;
}

static struct bdev* add_dev_match(struct ctx* ctx, int how)
{
	if(nbdevs >= NBDEVS)
		parse_error(ctx, "too many block dev entries", NULL);
	if(ctx->argc != 2)
		parse_error(ctx, "invalid arguments", NULL);

	ctx->devidx = nbdevs;

	struct bdev* bd = &bdevs[nbdevs++];

	memzero(bd, sizeof(*bd));

	bd->how = how;

	return bd;
}

static void add_text_id(struct ctx* ctx, int type)
{
	struct bdev* bd;

	if(!(bd = add_dev_match(ctx, type)))
		return;

	copy_sized(ctx, bd->id, sizeof(bd->id), 1);
}

static void add_hex_id(struct ctx* ctx, int type, int len)
{
	struct bdev* bd;

	if(!(bd = add_dev_match(ctx, type)))
		return;

	if(ctx->argc != 2)
		parse_error(ctx, "single argument required", NULL);

	char* q = ctx->args[1];

	char* s = bd->id;
	char* p = bd->id;
	char* e = bd->id + sizeof(bd->id) - 1;

	for(; *q && p < e; q++)
		switch(*q) {
			case '0' ... '9':
			case 'A' ... 'F': *p++ = *q; break;
			case 'a' ... 'f': *p++ = 'A' + (*q - 'a'); break;
			case '-': if((p - s) % 2 == 0) break; /* fallthrough */
			default: parse_error(ctx, "invalid hex value", NULL);
		}

	if(p - s != 2*len)
		parse_error(ctx, "incorrect id length", NULL);

	*p++ = '\0';
}

static void key_serial(struct ctx* ctx)
{
	add_text_id(ctx, BY_PG80);
}

static void key_cid(struct ctx* ctx)
{
	add_text_id(ctx, BY_CID);
}

static void key_mbr(struct ctx* ctx)
{
	add_hex_id(ctx, BY_MBR, 4);
}

static void key_gpt(struct ctx* ctx)
{
	add_hex_id(ctx, BY_GPT, 16);
}

static void set_key_index(struct ctx* ctx, struct part* pt, int keyidx)
{
	int ret;

	if(!keyidx)
		return;

	if((ret = check_keyindex(keyidx))) {
		if(ret == -ENOKEY)
			parse_error(ctx, "key index out of range", NULL);
		else
			parse_error(ctx, "encrypted partition", NULL);
	}

	pt->keyidx = keyidx;
}

static struct part* add_part_entry(struct ctx* ctx, int mode)
{
	if(nparts >= NPARTS)
		parse_error(ctx, "too many partitions", NULL);
	if(ctx->devidx < 0)
		parse_error(ctx, "partition without base device", NULL);

	struct bdev* bd = &bdevs[ctx->devidx];

	if(!bd->mode)
		bd->mode = mode;
	else if(bd->mode == PARTS && mode == PARTS)
		; /* it's ok */
	else if(mode == WHOLE)
		parse_error(ctx, "duplicate whole device entry", NULL);
	else
		parse_error(ctx, "cannot mix parts and whole", NULL);

	bd->mode = mode;

	struct part* pt = &parts[nparts++];
	memzero(pt, sizeof(*pt));

	pt->devidx = ctx->devidx;

	return pt;
}

static void key_part(struct ctx* ctx)
{
	int keyidx = 0;
	struct part* pt;

	if(!(pt = add_part_entry(ctx, PARTS)))
		return;
	if(ctx->argc < 3 || ctx->argc > 5)
		parse_error(ctx, "invalid arguments", NULL);

	copy_sized(ctx, pt->part, sizeof(pt->part), 1);
	copy_sized(ctx, pt->label, sizeof(pt->label), 2);
	parse_kidx(ctx, &keyidx, 3);
	copy_sized(ctx, pt->fs, sizeof(pt->fs), 4);

	set_key_index(ctx, pt, keyidx);
}

static void key_whole(struct ctx* ctx)
{
	int keyidx = 0;
	struct part* pt;

	if(!(pt = add_part_entry(ctx, WHOLE)))
		return;
	if(ctx->argc < 2 || ctx->argc > 4)
		parse_error(ctx, "invalid arguments", NULL);

	copy_sized(ctx, pt->label, sizeof(pt->label), 1);
	parse_kidx(ctx, &keyidx, 2);
	copy_sized(ctx, pt->fs, sizeof(pt->fs), 3);

	set_key_index(ctx, pt, keyidx);
}

static const struct kwd {
	char key[8];
	void (*call)(struct ctx* ctx);
} kwds[] = {
	{ "part",   key_part   },
	{ "whole",  key_whole  },
	{ "serial", key_serial },
	{ "cid",    key_cid    },
	{ "mbr",    key_mbr    },
	{ "gpt",    key_gpt    }
};

static void handle_conf(struct ctx* ctx)
{
	const struct kwd* p = kwds;
	const struct kwd* e = kwds + ARRAY_SIZE(kwds);

	if(!ctx->argc) return;

	char* key = *ctx->args;

	for(; p < e; p++)
		if(!strncmp(key, p->key, sizeof(p->key)))
			return p->call(ctx);

	parse_error(ctx, "unknown keyword", key);
}

static void* mmap_area(int need)
{
	int full = need + (PAGE - need % PAGE) % PAGE;

	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	void* ptr = sys_mmap(NULL, full, prot, flags, -1, 0);

	if(mmap_error(ptr))
		fail("mmap", NULL, (long)ptr);

	return ptr;
}

static int isspace(int c)
{
	return (c == ' ' || c == '\t');
}

static char* grab_arg(char* p, char** argi)
{
	*argi = p;

	while(*p && !isspace(*p)) p++;

	if(*p) *p++ = '\0';

	return p;
}

static void parse_line(struct ctx* ctx, char* line)
{
	int narg = 10;
	int argc = 0;
	char* args[narg];
	char* p = line;

	while(*p && isspace(*p))
		p++;
	if(*p == '#')
		return;

	while(*p) {
		if(argc >= narg)
			fail("too many args", NULL, 0); /* !!! */

		p = grab_arg(p, &args[argc++]);

		while(*p && isspace(*p))
			p++;
	}

	ctx->argc = argc;
	ctx->args = args;

	handle_conf(ctx);

	ctx->argc = 0;
	ctx->args = NULL;
}

static void parse_config(char* name, char* buf, char* end)
{
	struct ctx context = {
		.file = name,
		.line = 1,
		.devidx = -1
	};

	char* ls = buf;
	char* le;

	while(ls < end) {
		le = strecbrk(ls, end, '\n');
		*le = '\0';

		parse_line(&context, ls);

		ls = le + 1;
		context.line++;
	}
}

void load_config(void)
{
	int fd;
	long ret;
	struct stat st;
	char* name = BLKTAB;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail("cannot open", name, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("cannot stat", name, ret);
	if(st.size > MAX_CONFIG_SIZE)
		fail("file too large:", name, ret);

	uint len = st.size;
	char* buf;

	if(len + 1 <= sizeof(smallbuf))
		buf = smallbuf;
	else
		buf = mmap_area(len + 1);

	if((ret = sys_read(fd, buf, len)) < 0)
		fail("read", name, ret);
	else if((ulong)ret < len)
		fail("read", name, 0);

	sys_close(fd);

	buf[len] = '\0';

	return parse_config(name, buf, buf + len);
}
