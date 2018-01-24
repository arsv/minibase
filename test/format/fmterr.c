#include <bits/errno.h>
#include <errtag.h>
#include <format.h>
#include <string.h>
#include <util.h>

ERRLIST(NENOENT NEINTR NEINVAL);

static void test(char* file, int line, int err, char* exp)
{
	FMTBUF(p, e, buf, 100);
	p = fmterr(p, e, err);
	FMTEND(p, e);

	if(!strcmp(buf, exp))
		return;

	p = buf;
	p = fmtstr(p, e, file);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, line);
	p = fmtstr(p, e, ": FAIL ");
	p = fmtstr(p, e, buf);
	p = fmtstr(p, e, " expected ");
	p = fmtstr(p, e, exp);

	FMTENL(p, e);

	writeall(STDERR, buf, p - buf);
	_exit(0xFF);
}

#define FL __FILE__, __LINE__

int main(void)
{
	test(FL, -ENOENT, "ENOENT");
	test(FL, -EINTR,  "EINTR");
	test(FL, -EINVAL, "EINVAL");
	test(FL, -EPERM,  "-1"); /* hopefully same for all supported arches */

	return 0;
}
