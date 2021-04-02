#include <format.h>
#include <string.h>
#include <util.h>

static int test(char* file, int line, char* str, int exp)
{
	int res = strlen(str);

	if(res == exp)
		return 0;

	FMTBUF(p, e, buf, 200);

	p = fmtstr(p, e, file);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, line);
	p = fmtstr(p, e, ": ");
	p = fmtstr(p, e, "FAIL exp ");
	p = fmtint(p, e, exp);
	p = fmtstr(p, e, " got ");
	p = fmtint(p, e, res);

	FMTENL(p, e);

	writeall(STDERR, buf, p - buf);

	return -1;
}

#define TEST(str, exp) \
	ret |= test(__FILE__, __LINE__, str, exp)

int main(void)
{
	int ret = 0;

	TEST("", 0);
	TEST("a", 1);
	TEST("abc", 3);

	return ret;
}
