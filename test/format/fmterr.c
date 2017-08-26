#include <bits/errno.h>
#include <errtag.h>
#include <printf.h>
#include <format.h>
#include <string.h>

ERRLIST(NENOENT NEINTR NEINVAL);

static int test(char* file, int line, int err, char* exp)
{
	FMTBUF(p, e, buf, 30);
	p = fmterr(p, e, -err);
	FMTEND(p, e);

	if(!strcmp(buf, exp)) {
		printf("%s:%i: OK %s\n", file, line, buf);
		return 0;
	} else {
		printf("%s:%i: FAIL %s (expected %s)\n", file, line, buf, exp);
		return -1;
	}
}

#define TEST(err, str) \
	ret |= test(__FILE__, __LINE__, err, str);

int main(void)
{
	int ret = 0;

	TEST(ENOENT, "ENOENT");
	TEST(EINTR,  "EINTR");
	TEST(EINVAL, "EINVAL");
	TEST(EPERM,  "1"); /* hopefully same for all supported arches */

	return ret;
}
