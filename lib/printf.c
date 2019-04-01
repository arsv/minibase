/*
   printf implementation for printf-debugging. Not meant to be used
   in any other way. Non-debug code should use fmt* functions instead.

   Only stdout (printf) or stderr (tracef) output, with very limited
   format support:

               %Ni %0NX %s %p %m

   Max output length in a single call is defined by PRINTFBUF below.
   Anything above that will be silently dropped.
*/

#include <stdarg.h>
#include <string.h>
#include <format.h>
#include <printf.h>
#include <util.h>

#define F0 (1<<0) /* leading 0 */
#define Fm (1<<1) /* - (minus) */
#define Fl (1<<2) /* l (long)  */
#define Fd (1<<3) /* . (dot)   */

#define PRINTFBUF 512

struct spec {
	char c;
	short flags;
	short width;
	short prec;
};

static char* skip_to_fmt(char* s)
{
	while(*s && *s != '%')
		s++;
	return s;
}

static char* maybeint(char* q, int* dst)
{
	if(*q >= '0' && *q <= '9') {
		int num = 0;

		while(*q >= '0' && *q <= '9')
			num = num*10 + (*q++ - '0');

		*dst = num;
	} else {
		*dst = 0;
	}

	return q;
}

/* "If ap is passed to a function that uses va_arg(ap,type), then the value
    of ap is undefined after the return of that function." -- va_arg(3)

    This one has to be a big crappy function. */

static char* pprintf(char* p, char* e, const char* fmt, va_list ap)
{
	char* f = (char*)fmt;
	char* str;

	while(*f) {
		if(*f != '%') {
			char* t = f;
			f = skip_to_fmt(f);
			p = fmtraw(p, e, t, f - t);
			continue;
		};

		int flags = 0, width = 0, prec = 0;

		f++; /* skip % */

		/* parse format spec */

		if(*f == '-') { flags |= Fm; f++; }
		if(*f == '0') { flags |= F0; f++; }

		if(*f == '*')
			width = va_arg(ap, int);
		else
			f = maybeint(f, &width);

		if(*f == '.') { flags |= Fd; f++; }

		if(*f == '*')
			prec = va_arg(ap, int);
		else
			f = maybeint(f, &prec);

		if(*f == 'l') { flags |= Fl; f++; }

		/* pull and format argument */

		char* q;

		switch(*f++) {
			case 's':
				if(!(str = va_arg(ap, char*)))
					str = "(null)";
				if(flags & Fd)
					q = fmtstrn(p, e, str, prec);
				else
					q = fmtstr(p, e, str);
				break;
			case 'c':
				q = fmtchar(p, e, va_arg(ap, uint));
				break;
			case 'i':
				if(flags & Fl)
					q = fmtlong(p, e, va_arg(ap, long));
				else
					q = fmtint(p, e, va_arg(ap, int));
				break;
			case 'u':
				if(flags & Fl)
					q = fmtulong(p, e, va_arg(ap, ulong));
				else
					q = fmtuint(p, e, va_arg(ap, uint));
				break;
			case 'X':
			case 'x':
				if(flags & Fl)
					q = fmtxlong(p, e, va_arg(ap, ulong));
				else
					q = fmtxlong(p, e, va_arg(ap, uint));
				break;
			case 'p':
				q = fmtstr(p, e, "0x");
				q = fmtxlong(p, e, (long)va_arg(ap, void*));
				break;
			default:
				goto out;
		}

		/* pad the result if necessary */

		if(q - p >= width)
			p = q;
		else if(flags & F0)
			p = fmtpad0(p, e, width, q);
		else if(flags & Fm)
			p = fmtpadr(p, e, width, q);
		else
			p = fmtpad(p, e, width, q);
	}
out:
	return p;
}

long vsnprintf(char* buf, ulong len, const char* fmt, va_list ap)
{
	char* p = buf;
	char* e = buf + len;

	p = pprintf(p, e, fmt, ap);

	return p - buf;
}

int vfdprintf(int fd, const char* fmt, va_list ap)
{
	char buf[PRINTFBUF];
	int bufsize = sizeof(buf);
	char* p = buf;
	char* e = buf + bufsize;

	p = pprintf(p, e, fmt, ap);

	return writeall(fd, buf, p - buf);
}

int printf(const char* fmt, ...)
{
	int ret;
	va_list ap;

	va_start(ap, fmt);
	ret = vfdprintf(STDOUT, fmt, ap);
	va_end(ap);

	return ret;
}

int snprintf(char* buf, ulong len, const char* fmt, ...)
{
	int ret;
	va_list ap;

	va_start(ap, fmt);
	ret = vsnprintf(buf, len, fmt, ap);
	va_end(ap);

	return ret;
}

int putchar(int c)
{
        char cc = c;
        return writeall(STDOUT, &cc, 1);
}

int puts(const char* str)
{
        return writeall(STDOUT, (char*)str, strlen(str));
}

int tracef(const char* fmt, ...)
{
	int ret;
	va_list ap;

	va_start(ap, fmt);
	ret = vfdprintf(STDERR, fmt, ap);
	va_end(ap);

	return ret;
}
