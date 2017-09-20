#include <sys/file.h>
#include <errtag.h>
#include <format.h>
#include <util.h>

#define ERRBUF 512

void warn(const char* msg, const char* obj, int ret)
{
	char buf[ERRBUF];
	char* end = buf + sizeof(buf) - 1;
	char* b = buf;
	char* p = buf;

	if(errtag[0]) {
		p = fmtstr(p, end, errtag);
		p = fmtstr(p, end, ":");
	}
	if(msg) {
		p = fmtstr(p, end, " ");
		p = fmtstr(p, end, msg);
	} if(obj) {
		p = fmtstr(p, end, " ");
		p = fmtstr(p, end, obj);
	} if(ret && (msg || obj)) {
		p = fmtstr(p, end, ":");
	} if(ret) {
		p = fmtstr(p, end, " ");
		p = fmterr(p, end, ret);
	}

	*p++ = '\n';
	sys_write(2, b, p - b);
}

void fail(const char* msg, const char* obj, int ret)
{
	warn(msg, obj, ret);
	_exit(-1);
}
