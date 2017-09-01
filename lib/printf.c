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

static char* fmt_s(char* p, char* e, struct spec* sp, va_list ap)
{
	char* str = va_arg(ap, char*);

	if(!str)
		str = "(null)";

	if(sp->flags & Fd)
		return fmtstrn(p, e, str, sp->prec);
	else
		return fmtstr(p, e, str);
}

static char* fmt_c(char* p, char* e, struct spec* sp, va_list ap)
{
	return fmtchar(p, e, va_arg(ap, unsigned));
}

static char* fmt_i(char* p, char* e, struct spec* sp, va_list ap)
{
	if(sp->flags & Fl)
		return fmtlong(p, e, va_arg(ap, long));
	else
		return fmtint(p, e, va_arg(ap, int));
}

static char* fmt_u(char* p, char* e, struct spec* sp, va_list ap)
{
	if(sp->flags & Fl)
		return fmtulong(p, e, va_arg(ap, unsigned long));
	else
		return fmtuint(p, e, va_arg(ap, unsigned));
}

static char* fmt_x(char* p, char* e, struct spec* sp, va_list ap)
{
	if(sp->flags & Fl)
		return fmtxlong(p, e, va_arg(ap, unsigned long));
	else
		return fmtxlong(p, e, va_arg(ap, unsigned));
}

static char* fmt_p(char* p, char* e, struct spec* sp, va_list ap)
{
	p = fmtstr(p, e, "0x");
	p = fmtxlong(p, e, (long)va_arg(ap, void*));

	return p;
}

static char* dispatch(char* p, char* e, struct spec* sp, va_list ap)
{
	switch(sp->c) {
		case 's': return fmt_s(p, e, sp, ap);
		case 'c': return fmt_c(p, e, sp, ap);
		case 'i': return fmt_i(p, e, sp, ap);
		case 'u': return fmt_u(p, e, sp, ap);
		case 'X':
		case 'x': return fmt_x(p, e, sp, ap);
		case 'p': return fmt_p(p, e, sp, ap);
		default: return NULL;
	}
}

static char* format(char* p, char* e, struct spec* sp, va_list ap)
{
	char* q;

	if(!(q = dispatch(p, e, sp, ap)))
		return q;

	int width = sp->width;
	int flags = sp->flags;

	if(q - p >= width)
		return q;
	if(flags & F0)
		return fmtpad0(p, e, width, q);
	if(flags & Fm)
		return fmtpadr(p, e, width, q);
	else
		return fmtpad(p, e, width, q);

	return q;
}

static char* intpart(char* q, short* dst, va_list ap)
{
	if(*q >= '0' && *q <= '9') {
		int num = 0;

		while(*q >= '0' && *q <= '9')
			num = num*10 + (*q++ - '0');

		*dst = num;
	} else if(*q == '*') {
		q++;
		*dst = va_arg(ap, int);
	} else {
		*dst = 0;
	}

	return q;
}

static char* parse(char* q, struct spec* sp, va_list ap)
{
	short flags = 0;

	q++; /* skip % */

	if(*q == '-') { flags |= Fm; q++; }
	if(*q == '0') { flags |= F0; q++; }

	q = intpart(q, &sp->width, ap);

	if(*q == '.') { flags |= Fd; q++; }

	q = intpart(q, &sp->prec, ap);

	if(*q == 'l') { flags |= Fl; q++; }

	sp->c = *q++;
	sp->flags = flags;

	return q;
}

int vfdprintf(int fd, const char* fmt, va_list ap)
{
	char buf[PRINTFBUF];
	int bufsize = sizeof(buf);
	char* p = buf;
	char* e = buf + bufsize;

	char* f = (char*)fmt;
	struct spec sp;

	while(*f) {
		if(*f == '%') {
			if(!(f = parse(f, &sp, ap)))
				break;
			if(!(p = format(p, e, &sp, ap)))
				break;
		} else {
			char* t = f;
			f = skip_to_fmt(f);
			p = fmtraw(p, e, t, f - t);
		}
	}

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
