#include <sys/mman.h>

#include <printf.h>
#include <string.h>

#include "cmd.h"

/* Single argument parser for the Tab completition code.
   Given input line,

       /some/path> stat $HOME/Some\ long\ fil.....   <-- ctx.buf
                  ^sep                       ^cur

   it has to figure out where the argument being completed
   starts and ends:

       /some/path> stat $HOME/Some\ long\ fil.....   <-- ctx.buf
                        ^s                   ^e

   and turn it into directory name and file name prefix suited
   for open/getdents and strncmp against dirents:

       /home/alex₀Some long fil₀....                 <-- xa.buf
       ^dir       ^base

   The final names are stored in a short-lived, page-long mmaped
   buffer. It should probably move to the heap at some point, but
   care must be taken not to break insert() in case completition
   succeeds. */

static int isspace(int c)
{
	return (c == ' ' || c == '\t');
}

static int skip_space(char* buf, int s, int e)
{
	for(; s < e; s++)
		if(!isspace(buf[s]))
			break;

	return s;
}

static int skip_arg(char* buf, int s, int e)
{
	int expect = 0;

	for(; s < e; s++) {
		int c = buf[s];

		if(c == '\\')
			s++;
		else if(expect && c != expect)
			;
		else if(c == '\'')
			expect = '\'';
		else if(c == '"')
			expect = '"';
		else if(isspace(c))
			break;
	}

	return s;
}

static int mmap_exarg(struct exparg* xa)
{
	int flags = MAP_ANONYMOUS | MAP_PRIVATE;
	int prot = PROT_READ | PROT_WRITE;

	void* buf = sys_mmap(NULL, PAGE, prot, flags, -1, 0);

	if(mmap_error(buf))
		return (long)buf;

	xa->buf = buf;
	xa->base = buf;
	xa->buf[0] = '\0';

	return 0;
}

static char escaped(char c)
{
	if(c == 'n')
		return '\n';
	if(c == 't')
		return '\t';
	return c;
}

static void set_pointers(struct exparg* xa, char* s, char* e)
{
	char* p;

	if(s >= e) { /* empty arg */
		xa->dir = ".";
		xa->base = s;
		return;
	}

	for(p = e - 1; p > s; p--)
		if(*p == '/')
			break;

	if(p == s) { /* either "/base" or "base" */
		if(*p == '/') {
			xa->dir = "/";
			xa->base = p + 1;
		} else {
			xa->dir = ".";
			xa->base = p;
			xa->noslash = 1;
		}
	} else {
		*p = '\0';
		xa->dir = s;
		xa->base = p + 1;
	}

	xa->blen = strlen(xa->base);
}

static void parse_partial(CTX, struct exparg* xa, int sta, int end)
{
	char quote = '\0';

	char* sp = ctx->buf + sta;
	char* se = ctx->buf + end;

	char* ds = xa->buf;
	char* dp = ds;
	char* de = xa->buf + PAGE - 1;

	for(; sp < se && dp < de; sp++) {
		if(*sp == '\\') {
			if(++sp >= se)
				break;
			*dp++ = escaped(*sp);
		} else if(*sp == '\'') {
			if(!quote)
				quote = '\'';
			else if(quote == '\'')
				quote = '\0';
		} else if(*sp == '"') {
			if(!quote)
				quote = '"';
			else if(quote == '"')
				quote = '\0';
		} else {
			*dp++ = *sp;
		}
	}

	*dp = '\0';

	xa->quote = quote;

	set_pointers(xa, ds, dp);
}

int expand_arg(CTX, struct exparg* xa)
{
	char* buf = ctx->buf;
	int cur = ctx->cur;
	int sep = ctx->sep;
	int ret;

	int p, s = sep, e = cur;

	memzero(xa, sizeof(*xa));

	if((s = skip_space(buf, s, e)) >= e)
		return -ENOENT;

	int start = s;

	while((p = skip_arg(buf, s, e)) < e)
		s = skip_space(buf, p, e);

	if((ret = mmap_exarg(xa)))
		return ret;

	parse_partial(ctx, xa, s, e);

	if(s == start)
		xa->initial = 1;

	return 0;
}

void free_exparg(struct exparg* xa)
{
	sys_munmap(xa->buf, PAGE);
}

