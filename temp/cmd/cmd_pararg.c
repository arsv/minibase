#include <sys/mman.h>

#include <printf.h>
#include <string.h>
#include <util.h>

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

   The final result is stored in a short-lived, page-long mmaped
   buffer. It should probably get moved to the heap at some point,
   but care must be taken not to break insert() in case completition
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
	int size = PAGE;

	void* buf = sys_mmap(NULL, size, prot, flags, -1, 0);

	if(mmap_error(buf))
		return (long)buf;

	xa->buf = buf;
	xa->ptr = 0;
	xa->end = size - 1;

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

static void set_pointers(struct exparg* xa)
{
	char* p;

	char* s = xa->buf;
	char* e = s + xa->ptr;

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

static void addchar(XA, char c)
{
	if(xa->ptr >= xa->end)
		return;

	xa->buf[xa->ptr++] = c;
}

static void addstring(XA, char* str)
{
	int len = strlen(str);
	int left = xa->end - xa->ptr;

	if(len > left)
		len = left;
	if(len <= 0)
		return;

	memcpy(xa->buf, str, len);
	xa->ptr += len;
}

static int skip_variable(char* p, char* e)
{
	int i = 0;

	while(p + i < e) {
		char c = p[i];

		if(c >= '0' && c <= '9')
			;
		else if(c >= 'a' && c <= 'z')
			;
		else if(c >= 'A' && c <= 'Z')
			;
		else return i ? i : 1;

		i++;
	}

	return i;
}

static char* deref_variable(CTX, char* name, int nlen)
{
	char buf[nlen+1];

	memcpy(buf, name, nlen);
	buf[nlen] = '\0';

	return getenv(ctx->envp, buf);
}

static char* subst_tilde(CTX, struct exparg* xa, char* sp, char* se)
{
	char* home;

	if(sp + 1 < se && *(sp + 1) != '/')
		return sp;

	if((home = getenv(ctx->envp, "HOME")))
		addstring(xa, home);

	return sp + 1;
}

static int parse_partial(CTX, struct exparg* xa, int sta, int end)
{
	char quote = '\0';

	char* sp = ctx->buf + sta;
	char* se = ctx->buf + end;

	if(sp < se && *sp == '~')
		sp = subst_tilde(ctx, xa, sp, se);

	while(sp < se) {
		if(*sp == '\\') {
			if(++sp < se)
				addchar(xa, escaped(*sp++));
		} else if(*sp == '$' && quote != '\'') {
			int skip = skip_variable(++sp, se);
			char* val = deref_variable(ctx, sp, skip);
			if(!val) return -EFAULT;
			addstring(xa, val);
			sp += skip;
		} else if(*sp == '\'') {
			if(!quote)
				quote = '\'';
			else if(quote == '\'')
				quote = '\0';
			sp++;
		} else if(*sp == '"') {
			if(!quote)
				quote = '"';
			else if(quote == '"')
				quote = '\0';
			sp++;
		} else {
			addchar(xa, *sp++);
		}
	}

	xa->buf[xa->ptr] = '\0';
	xa->quote = quote;

	set_pointers(xa);

	return 0;
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

	if((ret = parse_partial(ctx, xa, s, e)) < 0)
		return ret;

	if(s == start)
		xa->initial = 1;

	return 0;
}

void free_exparg(struct exparg* xa)
{
	sys_munmap(xa->buf, PAGE);
}

