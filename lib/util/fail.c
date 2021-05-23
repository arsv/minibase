#include <sys/file.h>
#include <format.h>
#include <string.h>
#include <main.h>
#include <util.h>

void warn(const char* cmsg, const char* cobj, int ret)
{
	char* err = (char*)errtag;
	char* msg = (char*)cmsg;
	char* obj = (char*)cobj;

	char* eend = strpend(err);
	char* mend = strpend(msg);
	char* oend = strpend(obj);

	int elen = strelen(err, eend);
	int mlen = strelen(msg, mend);
	int olen = strelen(obj, oend);
	int size = elen + mlen + olen + 32;

	char* buf = alloca(size);
	char* p = buf;
	char* e = buf + size - 1;

	if(eend) {
		p = fmtraw(p, e, err, elen);
		p = fmtchar(p, e, ':');
	}
	if(mend) {
		p = fmtchar(p, e, ' ');
		p = fmtraw(p, e, msg, mlen);
	}
	if(oend) {
		p = fmtchar(p, e, ' ');
		p = fmtraw(p, e, obj, olen);
	}
	if(ret && (mlen || olen)) {
		p = fmtchar(p, e, ':');
	}
	if(ret) {
		p = fmtchar(p, e, ' ');
		p = fmterr(p, e, ret);
	}

	*p++ = '\n';

	sys_write(STDERR, buf, p - buf);
}

void fail(const char* msg, const char* obj, int ret)
{
	warn(msg, obj, ret);

	_exit(0xFF);
}
