#include <sys/file.h>
#include <sys/mman.h>
#include <sys/fprop.h>

#include <config.h>
#include <format.h>
#include <string.h>
#include <util.h>
#include <main.h>

#include "mpkg.h"

/* The config is parsed before loading the package index, in part to allow
   repo paths to be specified there. All the stringy values (from, into etc)
   get moved onto the stack, then the config gets unmmaped.

   The parsed path statements (pass/skip/deny) are assembled in ctx->paths.
   Later, after the package gets loaded, the deploy code will go over the
   paths and make sure the package complies.

   To make things a bit simpler, all non-path statements must preceede the
   paths ones. This way, paths are guaranteed to end up in a contiguous chunk
   on the heap so ctx->paths/paend are enough. */

#define CONFIG BASE_ETC "/packages"

#define MAXCONF (1<<16) /* bytes */

/* parser states (ctx->state) */

#define ST_OUT  0
#define ST_IN   1
#define ST_RULE 2
#define ST_POST 3

static int isspace(char c)
{
	return (c == ' ' || c == '\t');
}

static char* skip_nonspace(char* p, char* e)
{
	for(; p < e; p++)
		if(isspace(*p))
			break;

	return p;
}

static char* skip_space(char* p, char* e)
{
	for(; p < e; p++)
		if(!isspace(*p))
			break;

	return p;
}

static char* trim_right(char* p, char* e)
{
	while(p < e) {
		char* q = e - 1;

		if(!isspace(*q))
			break;

		e = q;
	}

	return e;
}

void fail_syntax(CTX, const char* msg, char* arg)
{
	int clen = strlen(ctx->config);
	int mlen = strlen(msg);
	int need = clen + mlen + 20;

	char* buf = alloca(need);

	char* p = buf;
	char* e = buf + need - 1;

	p = fmtstr(p, e, ctx->config);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, ctx->line);
	p = fmtstr(p, e, ": ");
	p = fmtstr(p, e, msg);

	if(arg) {
		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, arg);
	}

	*p++ = '\n';

	writeall(STDERR, buf, p - buf);

	_exit(0xFF);
}

static void key_group(CTX, char* p, char* e)
{
	char* group = ctx->group;

	if(!group) {
		ctx->state = ST_POST;
		return;
	}

	int gl = strlen(group);
	int al = e - p;

	if((gl == al) && !memcmp(p, group, gl)) {
		if(ctx->state == ST_OUT)
			ctx->state = ST_IN;
		else
			fail_syntax(ctx, "duplicate group", NULL);
	} else {
		if(ctx->state != ST_OUT)
			ctx->state = ST_POST;
	}
}

static char* copy_rel_path(CTX, char* arg, char* end)
{
	char* here = HERE;
	int alen = end - arg;
	int hlen = strlen(here);
	int need = hlen + alen + 2;

	char* buf = alloc_align(ctx, need);

	char* p = buf;
	char* e = buf + need - 1;

	p = fmtraw(p, e, here, hlen);
	p = fmtchar(p, e, '/');
	p = fmtraw(p, e, arg, alen);

	*p++ = '\0';

	return buf;
}

static char* copy_abs_path(CTX, char* arg, char* end)
{
	int len = end - arg;
	int need = len + 1;

	char* buf = alloc_align(ctx, need);

	memcpy(buf, arg, len);

	buf[len] = '\0';

	return buf;
}

static void set_absrel(CTX, char* arg, char* end, char** dst)
{
	int state = ctx->state;

	if(state == ST_OUT)
		return;
	if(state == ST_POST)
		return;

	if(state == ST_RULE)
		fail_syntax(ctx, "misplaced keyword", NULL);
	if(*dst)
		fail_syntax(ctx, "duplicate statement", NULL);
	if(arg >= end)
		fail_syntax(ctx, "empty argument", NULL);

	char* str;

	if(*arg == '/')
		str = copy_abs_path(ctx, arg, end);
	else
		str = copy_rel_path(ctx, arg, end);

	*dst = str;
}

static void key_from(CTX, char* p, char* e)
{
	set_absrel(ctx, p, e, &(ctx->repodir));
}

static void key_into(CTX, char* p, char* e)
{
	set_absrel(ctx, p, e, &(ctx->prefix));
}

static inline uint align4(uint x)
{
	return (x + 3) & ~3;
}

static void append_rule(CTX, char* p, char* e, int flag)
{
	struct path* pp;

	int len = e - p;
	int need = align4(sizeof(*pp) + len + 1);

	if(p >= e)
		fail_syntax(ctx, "empty path", NULL);
	if(len >= 4096)
		fail_syntax(ctx, "path too long", NULL);

	pp = alloc_exact(ctx, need);

	pp->hlen = need | flag;
	memcpy(pp->str, p, len);
	pp->str[len] = '\0';
}

/* There are two ways of describing the allowed package tree:

       pass, pass, pass (and deny the rest), or
       deny, deny, deny (and pass the rest)

   Having both pass and deny statements in the same group makes no sense.

   The original approach was to only have deny-rest option, but there may
   be cases where pass-rest would be a more natural approach for the user,
   so it was added. Having this second option also allows makes handling
   the no-rules case (pass-all) more straightforward. */

static void update_policy(CTX, int flag)
{
	int pol = ctx->policy;

	if(flag & HLEN_PASS) {
		if(pol == POL_PASS_REST)
			fail_syntax(ctx, "pass after deny", NULL);

		pol = POL_DENY_REST;
	} else if(flag & HLEN_DENY) {
		if(pol == POL_DENY_REST)
			fail_syntax(ctx, "deny after pass", NULL);

		pol = POL_PASS_REST;
	}

	ctx->policy = pol;
}

static void common_path(CTX, char* p, char* e, int flag)
{
	int state = ctx->state;

	if(state == ST_OUT)
		return;
	if(state == ST_POST)
		return;

	if(state == ST_IN) {
		ctx->paths = ctx->ptr;
		ctx->state = ST_RULE;
	}

	if(p >= e)
		fail_syntax(ctx, "empty path", NULL);
	if(*p == '/')
		fail_syntax(ctx, "absolute path", NULL);

	append_rule(ctx, p, e, flag);
	update_policy(ctx, flag);
}

static void key_pass(CTX, char* p, char* e)
{
	common_path(ctx, p, e, HLEN_PASS);
}

static void key_skip(CTX, char* p, char* e)
{
	common_path(ctx, p, e, HLEN_SKIP);
}

static void key_deny(CTX, char* p, char* e)
{
	common_path(ctx, p, e, HLEN_DENY);
}

static void key_suffix(CTX, char* p, char* e)
{
	int state = ctx->state;

	if(state == ST_OUT)
		return;
	if(state == ST_POST)
		return;

	if(state == ST_RULE)
		fail_syntax(ctx, "misplaced keyword", NULL);
	if(ctx->suffix)
		fail_syntax(ctx, "duplicate statement", NULL);

	ctx->suffix = copy_string(ctx, p, e);
}

static const struct key {
	char tag[8];
	void (*call)(CTX, char* p, char* e);
} keys[] = {
	{ "group",  key_group  },
	{ "suffix", key_suffix },
	{ "into",   key_into   },
	{ "from",   key_from   },
	{ "pass",   key_pass   },
	{ "skip",   key_skip   },
	{ "deny",   key_deny   },
};

static void dispatch_keyword(CTX, char* kp, char* ke, char* ap, char* ae)
{
	const struct key* ky;

	for(ky = keys; ky < ARRAY_END(keys); ky++) {
		int tl = strnlen(ky->tag, sizeof(ky->tag));
		int kl = ke - kp;

		if(kl != tl)
			continue;
		if(memcmp(kp, ky->tag, kl))
			continue;

		return ky->call(ctx, ap, ae);
	}

	fail_syntax(ctx, "unknown keyword", NULL);
}

static void parse_line(CTX, char* p, char* e)
{
	char* q;

	p = skip_space(p, e);

	if(p >= e) /* empty line */
		return;
	if(*p == '#')
		return;

	q = skip_nonspace(p, e);

	char* kp = p;
	char* ke = q;

	p = skip_space(q, e);
	e = trim_right(p, e);

	dispatch_keyword(ctx, kp, ke, p, e);
}

static void parse_config(CTX, char* buf, uint size)
{
	char* p = buf;
	char* e = buf + size;

	if(ctx->group)
		ctx->state = ST_OUT;
	else
		ctx->state = ST_IN;

	ctx->paths = NULL;

	while(p < e) {
		char* q = strecbrk(p, e, '\n');

		ctx->line++;

		parse_line(ctx, p, q);

		p = q + 1;
	};

	if(ctx->group && ctx->state == ST_OUT)
		fail("unknown group", ctx->group, 0);

	if(!ctx->paths)
		ctx->paths = ctx->ptr;

	ctx->paend = ctx->ptr;
}

void load_config(CTX)
{
	int fd, ret;
	struct stat st;
	char* name = CONFIG;

	ctx->config = name;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", name, ret);

	if(st.size >= MAXCONF)
		fail(NULL, name, -E2BIG);

	uint size = st.size;
	int proto = PROT_READ;
	int flags = MAP_PRIVATE;
	void* buf = sys_mmap(NULL, size, proto, flags, fd, 0);

	if((ret = mmap_error(buf)))
		fail("mmap", name, ret);

	parse_config(ctx, buf, size);

	if((ret = sys_close(fd)) < 0)
		fail("close", NULL, ret);
	if((ret = sys_munmap(buf, size)) < 0)
		fail("munmap", NULL, ret);
}
