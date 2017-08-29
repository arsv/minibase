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

#define F0 (1<<0) /* got leading 0 in width field */
#define Fm (1<<1) /* got - (minus) in format spec */
#define Fl (1<<2) /* got l (long) prefix */
#define Fd (1<<3) /* got . (dot) */

#define PRINTFBUF 512

static int skiptofmt(const char* s)
{
	const char* p;
	for(p = s; *p && *p != '%'; p++);
	return p - s;
}

char* fmtpadr(char* p, char* e, int num, char* q)
{
	int len = q - p;

	while(len++ < num)
		q = fmtchar(q, e, ' ');

	return q;
}

char* fmtfpad(char* p, char* e, char* q, int flags, int num)
{
	if(!num)
		return q;
	if(flags & F0)
		return fmtpad0(p, e, num, q);
	if(flags & Fm)
		return fmtpadr(p, e, num, q);
	else
		return fmtpad(p, e, num, q);
}

static char* fmtstr_(char* p, char* e, char* str, int width, int flags)
{
	if(!str)
		str = "(null)";
	if(flags & Fd)
		return fmtstrn(p, e, str, width);
	else
		return fmtstr(p, e, str);
}

int vfdprintf(int fd, const char* fmt, va_list ap)
{
	char buf[PRINTFBUF];
	int bufsize = sizeof(buf);
	char* p = buf;
	char* e = buf + bufsize;
	char* q;

	while(*fmt) {
		if(*fmt != '%') {
			int len = skiptofmt((char*)fmt);
			p = fmtstrn(p, e, fmt, len);
			fmt += len;
			continue;
		}

		fmt++;
		
		int num = 0, flags = 0;

		if(*fmt == '-') { flags |= Fm; fmt++; }
		if(*fmt == '.') { flags |= Fd; fmt++; }

		if(*fmt == '*') {
			num = va_arg(ap, int);
			fmt++;
		} else while(*fmt >= '0' && *fmt <= '9') {
			if(!num && *fmt == '0')
				flags |= F0;
			num = num*10 + (*fmt++ - '0');
		}

		if(*fmt == '*' && !num) { fmt++; num = va_arg(ap, int); }

		if(*fmt == 'l') { flags |= Fl; fmt++; }

		switch(*fmt++) {
		case 's':
			q = fmtstr_(p, e, va_arg(ap, char*), num, flags);
			break;
		case 'c':
			q = fmtchar(p, e, va_arg(ap, unsigned));
			break;
		case 'i':
			if(flags & Fl)
				q = fmtlong(p, e, va_arg(ap, long));
			else
				q = fmtint(p, e, va_arg(ap, int));
			break;
		case 'u':
			if(flags & Fl)
				q = fmtlong(p, e, va_arg(ap, unsigned long));
			else
				q = fmtint(p, e, va_arg(ap, unsigned));
			break;
		case 'X':
			if(flags & Fl)
				q = fmtxlong(p, e, va_arg(ap, unsigned long));
			else
				q = fmtxlong(p, e, va_arg(ap, unsigned));
			break;
		case 'p':
			q = fmtxlong(p, e, (long)va_arg(ap, void*));
			break;
		default: goto out;
		};

		p = fmtfpad(p, e, q, flags, num);
	}
out:
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
