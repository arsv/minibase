/*
   printf implementation for printf-debugging. Not meant to be used
   in any other way. Non-debug code should use fmt* functions instead.

   Only fd 1 output, and a minimalistic format support:

               %Ni %0NX %s %p %m

   Max output length in a single call is defined by PRINTFBUF below.
   Anything above that will be silently dropped.
*/

#include <stdarg.h>
#include <string.h>
#include <format.h>
#include <printf.h>
#include <util.h>

#define F0 (1<<0)
#define Fm (1<<1)
#define Fl (1<<2)

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

		while(*fmt >= '0' && *fmt <= '9') {
			if(!num && *fmt == '0') flags |= F0;
			num = num*10 + (*fmt++ - '0');
		}

		if(*fmt == 'l') { flags |= Fl; fmt++; }

		switch(*fmt++) {
		case 's':
			q = fmtstr(p, e, va_arg(ap, char*));
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
