#include <format.h>
#include <util.h>

#include "init.h"

static const char tag[] = "init";
static char warnbuf[200];

void report(char* msg, char* arg, int err)
{
	char* p = warnbuf;
	char* e = warnbuf + sizeof(warnbuf) - 1;

	if(gg.outfd <= STDERR) {
		p = fmtstr(p, e, tag);
		p = fmtstr(p, e, ": ");
	};

	p = fmtstr(p, e, msg);

	if(arg) {
		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, arg);
	};

	if(err) {
		p = fmtstr(p, e, ": ");
		p = fmtint(p, e, err);
	};

	*p++ = '\n';

	writeall(gg.outfd, warnbuf, p - warnbuf);
}

void reprec(struct initrec* rc, char* msg)
{
	char* p = warnbuf;
	char* e = warnbuf + sizeof(warnbuf) - 1;

	p = fmtstr(p, e, rc->name);
	p = fmtstr(p, e, ": ");
	p = fmtstr(p, e, msg);

	*p++ = '\n';

	writeall(gg.outfd, warnbuf, p - warnbuf);
}
