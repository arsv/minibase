#include <string.h>

#include "common.h"

/* File parsing code for modprobe. Nothing really complex but still worth
   checking separately from the module-handing stuff.

   In all relevant cases (modules.dep, modules.alias, modules.conf etc),
   the goal is to locate a line starting with a particular prefix
   and then use whatever follows that prefix. */

typedef char* (*lnmatch)(char* ls, char* le, char* name);

static int locate_line(struct mbuf* mb, struct line* ln, lnmatch lnm, char* name)
{
	char* bs = mb->buf;
	char* be = bs + mb->len;
	char *ls, *le, *p;

	for(ls = bs; ls < be; ls = le + 1) {
		le = strecbrk(ls, be, '\n');

		if(!(p = lnm(ls, le, name)))
			continue;

		ln->ptr = ls;
		ln->end = le;
		ln->sep = p;

		if(p < le && !isspace(*p))
			p++;
		while(p < le && isspace(*p))
			p++;

		ln->val = p;

		return 0;
	}

	return -ENOENT;
}

/* Sometimes a module named foo-bar resides in a file named foo_bar.ko
   and vice versa. There are no apparent rules for this, so we just
   collate - with _ and match the names that way. */

static char eq(char c)
{
	return (c == '_' ? '-' : c);
}

static int xstrncmp(char* a, char* b, int len)
{
	char* e = b + len;

	while(*a && b < e && *b)
		if(eq(*a++) != eq(*b++))
			return -1;

	if(b >= e)
		return 0;
	if(*a == *b)
		return 0;

	return -1;
}

static char* match_dep(char* ls, char* le, char* name)
{
	int nlen = strlen(name);
	char* p = strecbrk(ls, le, ':');

	if(p >= le)
		return NULL;

	char* q = p - 1;

	while(q > ls && *q != '/') q--;

	if(*q == '/') q++;

	if(le - q < nlen)
		return NULL;

	if(xstrncmp(q, name, nlen))
		return NULL;
	if(q[nlen] != '.')
		return NULL;

	return p;
}

static char* match_builtin(char* ls, char* le, char* name)
{
	int nlen = strlen(name);
	char* q = le - 1;

	while(q > ls && *q != '/') q--;

	if(*q == '/') q++;

	if(le - q < nlen)
		return NULL;
	if(xstrncmp(q, name, nlen))
		return NULL;
	if(q[nlen] != '.')
		return NULL;

	return q;
}

static char* word(char* p, char* e, char* w)
{
	int len = strlen(w);

	if(e - p < len)
		return NULL;
	if(strncmp(p, w, len))
		return NULL;
	if(p + len >= e)
		return e;

	p += len;

	if(!isspace(*p))
		return NULL;

	return p;
}

static char* skip(char* p, char* e, char* w)
{
	if((p = word(p, e, w)))
		while(p < e && isspace(*p))
			p++;

	return p;
}

static char* match_opt(char* ls, char* le, char* name)
{
	char* p = ls;
	char* e = le;

	if(!(p = skip(p, e, "options")))
		return NULL;
	if(!(p = word(p, e, name)))
		return NULL;

	if(p > ls && isspace(*(p-1))) p--;

	return p;
}

static char* match_blacklist(char* ls, char* le, char* name)
{
	char* p = ls;
	char* e = le;

	if(!(p = skip(p, e, "blacklist")))
		return NULL;
	if(!(p = word(p, e, name)))
		return NULL;

	return p;
}

/* Note wildcard handling here is wrong, but it's enough to get kernel
   aliases working.

   pci:v000010ECd0000525Asv00001028sd000006DEbcFFsc00i00
   pci:v000010ECd0000525Asv*       sd*       bc* sc* i*

   The values being matched with * are uppercase hex while the terminals
   are lowercase letters, so we can just skip until the next non-* character
   from the pattern. */

static char* match_alias(char* ls, char* le, char* name)
{
	char* p = ls;
	char* e = le;
	char* n = name;

	if(!(p = skip(p, e, "alias")))
		return NULL;

	while(*n && p < e) {
		if(isspace(*p)) {
			break;
		} else if(*p == '*') {
			p++;
			while(*n && *n != *p)
				n++;
		} else if(*p != *n) {
			return NULL;
		} else {
			p++;
			n++;
		};
	}

	if(p < e && *p == '*')
		p++; /* skip trailing * matching nothing */
	if(*n || p >= e || !isspace(*p))
		return NULL;

	return p;
}

int locate_dep_line(struct mbuf* mb, struct line* ln, char* name)
{
	return locate_line(mb, ln, match_dep, name);
}

int locate_opt_line(struct mbuf* mb, struct line* ln, char* name)
{
	return locate_line(mb, ln, match_opt, name);
}

int locate_alias_line(struct mbuf* mb, struct line* ln, char* name)
{
	return locate_line(mb, ln, match_alias, name);
}

int locate_blist_line(struct mbuf* mb, struct line* ln, char* name)
{
	return locate_line(mb, ln, match_blacklist, name);
}

int locate_built_line(struct mbuf* mb, struct line* ln, char* name)
{
	return locate_line(mb, ln, match_builtin, name);
}
