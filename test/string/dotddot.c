#include <sys/dents.h>

#include <format.h>
#include <util.h>

#define TEST(ret, str) test(__FILE__, __LINE__, str, ret, dotddot(str))

static void test(char* file, int line, char* str, int exp, int got)
{
	if(!!got == !!exp)
		return;

	FMTBUF(p, e, buf, 100);
	p = fmtstr(p, e, file);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, line);
	p = fmtstr(p, e, ": FAIL \"");
	p = fmtstr(p, e, str);
	p = fmtstr(p, e, "\" = ");
	p = fmtint(p, e, got);
	p = fmtstr(p, e, " expected ");
	p = fmtint(p, e, exp);
	FMTENL(p, e);

	writeall(STDERR, buf, p - buf);

	_exit(0xFF);
}

int main(void)
{
	TEST(0, "");
	TEST(1, ".");
	TEST(1, "..");
	TEST(0, "...");
	TEST(0, ".a");
	TEST(0, "abc");

	return 0;
}
