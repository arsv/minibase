#include <format.h>
#include <util.h>
#include "super.h"

/* Error reporting. PID 1 cannot use fail() so no point in bringing
   that here. Also at some point reprec should probably start sending
   messages to syslog if it's available. */

static const char tag[] = "super";
static char warnbuf[200];

void report(char* msg, char* arg, int err)
{
	char* p = warnbuf;
	char* e = warnbuf + sizeof(warnbuf) - 1;

	p = fmtstr(p, e, tag);
	p = fmtstr(p, e, ": ");

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

	writeall(STDERR, warnbuf, p - warnbuf);
}

void reprec(struct proc* rc, char* msg)
{
	char* p = warnbuf;
	char* e = warnbuf + sizeof(warnbuf) - 1;

	p = fmtstr(p, e, rc->name);
	p = fmtstr(p, e, ": ");
	p = fmtstr(p, e, msg);

	*p++ = '\n';

	writeall(STDERR, warnbuf, p - warnbuf);
}
