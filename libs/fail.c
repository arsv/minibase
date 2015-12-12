#include <sys/write.h>

#include <strint32.h>
#include <strapp.h>
#include <fail.h>

#define ERRBUF 512

extern void _exit(int code) __attribute__((noreturn));

extern ERRTAG;
extern ERRLIST;

static char* strerr(char* buf, char* end, int err)
{
	const struct errcode* p;

	for(p = errlist; p->code; p++)
		if(p->code == err)
			break;
	if(p->code)
		return strapp(buf, end, p->name);
	else
		return stri32(buf, end, err);
};

void warn(const char* msg, const char* obj, int err)
{
	char buf[ERRBUF];
	char* end = buf + sizeof(buf) - 1;
	char* b = buf;
	char* p = buf;

	p = strapp(p, end, errtag);
	p = strapp(p, end, ":");

	if(msg) {
		p = strapp(p, end, " ");
		p = strapp(p, end, msg);
	} if(obj) {
		p = strapp(p, end, " ");
		p = strapp(p, end, obj);
	} if(err) {
		p = strapp(p, end, ": ");
		p = strerr(p, end, err);
	}

	*p++ = '\n';
	syswrite(2, b, p - b);
}

void fail(const char* msg, const char* obj, int err)
{
	warn(msg, obj, err);
	_exit(-1);
}
