#include <sys/file.h>
#include <sys/mman.h>

#include <format.h>
#include <string.h>
#include <util.h>

#include "common.h"
#include "keymon.h"

#define MAX_CONFIG_SIZE 0x10000

/* The code here loads the (plain text) config file and compiles it
   into internal representation, which is a bunch of `struct act`
   packed into ctx->acts (a buffer ACLEN bytes long). */

struct lbuf {
	char* file;
	int line;
	char* ls;
	char* le;
};

struct act* first(CTX)
{
	void* acts = ctx->acts;
	void* aend = acts + ctx->aclen;

	struct act* ka = acts;

	if(acts + ka->len > aend)
		return NULL;

	return ka;
}

struct act* next(CTX, struct act* ka)
{
	void* acts = ctx->acts;
	void* aend = acts + ctx->aclen;
	void* ptr = ka;

	if(ptr < acts)
		return NULL;
	if(ptr + sizeof(*ka) > aend)
		return NULL;

	int len = ka->len;

	if(len < sizeof(*ka))
		return NULL;

	ptr += (len + 3) & ~3;
	ka = ptr;

	if(ptr + sizeof(*ka) > aend)
		return NULL;
	if(ptr + ka->len > aend)
		return NULL;

	return ka;
}

static noreturn void error(struct lbuf* lb, char* msg)
{
	FMTBUF(p, e, buf, 100);

	p = fmtstr(p, e, lb->file);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, lb->line);
	p = fmtstr(p, e, ": ");
	p = fmtstr(p, e, msg);

	FMTENL(p, e);

	writeall(STDERR, buf, p - buf);

	_exit(0xFF);
}

static struct act* add_act(CTX, struct lbuf* lb)
{
	int ptr = ctx->aclen;
	int cap = ACLEN;
	int add = sizeof(struct act);

	if(ptr + add > cap)
		error(lb, "out of action space");

	ctx->aclen = ptr + add;

	return ctx->acts + ptr;
}

static void add_arg(CTX, struct lbuf* lb, char* p, char* e)
{
	int len = e - p;
	int ptr = ctx->aclen;
	char* buf = ctx->acts + ptr;

	if(ptr + len + 1 > ACLEN)
		error(lb, "out of action space");

	memcpy(buf, p, len);
	buf[len] = '\0';

	ctx->aclen = ptr + len + 1;
}

static void end_act(CTX, struct lbuf* lb, struct act* ka)
{
	int ptr = ctx->aclen;
	void* act = ka;
	void* end = ctx->acts + ptr;

	if(act + sizeof(*ka) == end)
		error(lb, "missing command");

	ptr = (ptr + 3) & ~3; /* align to 4 bytes */

	ka->len = end - act;

	ctx->aclen = ptr;
}

static int isspace(int c)
{
	return (c == ' ' || c == '\t');
}

static char* skip_spec(struct lbuf* lb, char* p, char* e)
{
	for(; p < e; p++)
		if(*p == ':')
			return p;

	error(lb, "missing key spec");
}

static char* skip_space(char* p, char* e)
{
	while(p < e && isspace(*p))
		p++;

	return p;
}

static char* skip_word(char* p, char* e)
{
	while(p < e && !isspace(*p))
		p++;

	return p;
}

static char* prefix(char* p, char* e, char* word, int wlen)
{
	if(e - p < wlen)
		return NULL;
	if(memcmp(p, word, wlen))
		return NULL;

	return p + wlen;
}

static char* leader(char* p, char* e, char* word, int wlen)
{
	char* end;

	if((end = prefix(p, e, word, wlen)))
		end = skip_space(end, e);

	return end;
}

static void parse_key(struct act* ka, struct lbuf* lb, char* p, char* e)
{
	char* q;
	int code;
	int mode;

	if((q = leader(p, e, "hold", 4))) {
		mode = MODE_HOLD;
		p = q;
		goto key;
	}
	if((q = leader(p, e, "long", 4))) {
		mode = MODE_HOLD | MODE_LONG;
		p = q;
		goto key;
	}

	mode = 0;
key:
	if((q = prefix(p, e, "C-", 2))) {
		mode |= MODE_CTRL;
		p = q;
	}
	if((q = prefix(p, e, "A-", 2))) {
		mode |= MODE_ALT;
		p = q;
	}

	if(!(code = find_key(p, e - p)))
		error(lb, "unknown key code");

	ka->code = code;
	ka->mode = mode;
	ka->pid = 0;
}

static void parse_line(CTX, struct lbuf* lb, char* ls, char* le)
{
	struct act* ka;
	char *p, *e;

	char* sep = skip_spec(lb, ls, le);

	ka = add_act(ctx, lb);

	parse_key(ka, lb, ls, sep);

	for(p = sep + 1; p < le; p = e) {
		p = skip_space(p, le);
		e = skip_word(p, le);

		if(e == p) break;

		add_arg(ctx, lb, p, e);
	}

	end_act(ctx, lb, ka);
}

static void parse_config(CTX, char* name, char* buf, char* end)
{
	struct lbuf lb = { name, 0 };
	char *ls, *le;

	for(ls = buf; ls < end; ls = le + 1) {
		lb.line++;
		le = strecbrk(ls, end, '\n');

		while(ls < le && isspace(*ls))
			ls++;
		if(ls >= le)
			continue;
		if(*ls == '#')
			continue;

		parse_line(ctx, &lb, ls, le);
	}
}

void load_config(CTX)
{
	int fd, ret;
	struct stat st;
	char* name = KEYCONF;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail("cannot open", name, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("cannot stat", name, ret);
	if(st.size > MAX_CONFIG_SIZE)
		fail("file too large:", name, ret);

	int len = st.size;
	int prot = PROT_READ;
	int flags = MAP_PRIVATE;
	void* buf = sys_mmap(NULL, len, prot, flags, fd, 0);

	if(mmap_error(buf))
		fail("cannot mmap", name, (long)buf);

	parse_config(ctx, name, buf, buf + len);

	if((ret = sys_close(fd)) < 0)
		fail("close", name, ret);
	if((ret = sys_munmap(buf, len)) < 0)
		fail("munmap", name, ret);
}
