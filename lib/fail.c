#include <sys/write.h>

#include <fmtint32.h>
#include <fmtstr.h>
#include <fail.h>

#define ERRBUF 512

extern void _exit(int code) __attribute__((noreturn));

extern ERRTAG;
extern ERRLIST;

static char* fmterr(char* buf, char* end, int err)
{
	const struct errcode* p;

	for(p = errlist; p->code; p++)
		if(p->code == err)
			break;
	if(p->code)
		return fmtstr(buf, end, p->name);
	else
		return fmti32(buf, end, err);
};

void warn(const char* msg, const char* obj, int err)
{
	char buf[ERRBUF];
	char* end = buf + sizeof(buf) - 1;
	char* b = buf;
	char* p = buf;

	p = fmtstr(p, end, errtag);
	p = fmtstr(p, end, ":");

	if(msg) {
		p = fmtstr(p, end, " ");
		p = fmtstr(p, end, msg);
	} if(obj) {
		p = fmtstr(p, end, " ");
		p = fmtstr(p, end, obj);
	} if(err && (msg || obj)) {
		p = fmtstr(p, end, ":");
	} if(err) {
		p = fmtstr(p, end, " ");
		p = fmterr(p, end, err);
	}

	*p++ = '\n';
	syswrite(2, b, p - b);
}

void fail(const char* msg, const char* obj, int err)
{
	warn(msg, obj, err);
	_exit(-1);
}
