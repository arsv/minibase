#include <sys/write.h>

#include <itostr.h>
#include <strapp.h>
#include <fail.h>

#define ERRBUF 512

extern void _exit(int code) __attribute__((noreturn));

static char* strerr(char* buf, char* end, int err)
{
	const struct errcode* p;

	for(p = errlist; p->code; p++)
		if(p->code == err)
			break;
	if(p->code)
		return strapp(buf, end, p->name);
	else
		return itostr(buf, end, err);
};

void fail(const char* tag, const char* msg, const char* obj, int err)
{
	char buf[ERRBUF];
	char* end = buf + sizeof(buf) - 1;
	char* b = buf;
	char* p = buf;

	if(tag) {
		p = strapp(p, end, tag);
		p = strapp(p, end, ": ");
	} if(msg) {
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
	_exit(-1);
}
