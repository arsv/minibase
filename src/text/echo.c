#include <alloca.h>
#include <argsmerge.h>
#include <writeall.h>

#define TAG "echo"
#define OPT_n (1<<0)
#define OPT_e (1<<1)

static int hexdigit(char c)
{
	switch(c) {
		case '0' ... '9': return c - '0';
		case 'a' ... 'f': return c - 'a' + 0x0A;
		case 'A' ... 'F': return c - 'A' + 0x0A;
		default: return -1;
	}
}

static char hexchar(char** pp)
{
	int d, c = 0;
	char* p = *pp;

	if((d = hexdigit(*p)) >= 0) { p++; c = d; }            else goto out;
	if((d = hexdigit(*p)) >= 0) {      c = (c << 4) | d; } else goto out;

out:	*pp = p;
	return c;
}

static char octchar(char** pp)
{
	int d, c = 0;
	char* p = *pp;

	if((d = *p - '0') >= 0 && d < 8) { p++; c = d;            } else goto out;
	if((d = *p - '0') >= 0 && d < 8) { p++; c = (c << 3) | d; } else goto out;
	if((d = *p - '0') >= 0 && d < 8) {      c = (c << 3) | d; } else goto out;

out:	*pp = p;
	return c;
}

static char* parse(char* buf, char* end)
{
	char* p = buf;
	char* q = buf;

	for(; p < end; p++)
		if(*p != '\\') {
			*q++ = *p;
		} else switch(*++p) {
			case 'n': *q++ = '\n'; break;
			case 't': *q++ = '\t'; break;
			case 'r': *q++ = '\r'; break;
			case 'x': *q++ = hexchar(&p); break;
			case '0': *q++ = octchar(&p); break;
		};

	return q;
}

/* The code below does the echo.
   The code above -- all of the code above -- is there only to support -e

   Sadly there is no way around it, -e does get some usage in scripts
   and is not easily replaceable.
   This can't be fixed without fixing the shell, and with that, echo
   should be dropped in favor of wrt anyway.

   So let's stay compatible on this one. */

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0;
	char* p;

	if(i < argc && argv[i][0] == '-')
		for(p = argv[i++] + 1; *p; p++) switch(*p) {
			case 'n': opts |= OPT_n; break;
			case 'e': opts |= OPT_e; break;
			default: return -1;
		}

	argc -= i;
	argv += i;

	if(argc <= 0)
		return 0;

	int len = argsumlen(argc, argv) + argc;
	char* buf = alloca(len + 5); /* parse() may overshoot for at most 3 chars */
	char* end = argsmerge(buf, buf + len, argc, argv);
	*end = '\0'; /* terminate any trailing \-sequence */

	if(opts & OPT_e)
		end = parse(buf, end);

	if(!(opts & OPT_n))
		*end++ = '\n';

	return writeall(1, buf, end - buf) >= 0 ? 0 : -1;
}
