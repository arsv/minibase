#include <format.h>
#include <string.h>
#include <util.h>

#define EQ 0
#define LT -1
#define GT  1

static char* opname(int op)
{
	if(op < 0)
		return "LT";
	if(op > 0)
		return "GT";
	return "EQ";
}

static int test(char* file, int line, int op, char* a, char* b)
{
	int res = strcmp(a, b);

	FMTBUF(p, e, buf, 200);

	p = fmtstr(p, e, file);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, line);
	p = fmtstr(p, e, ": ");

	if(op < 0 && res < 0)
		return 0;
	if(op > 0 && res > 0)
		return 0;
	if(!op && !res)
		return 0;

	p = fmtstr(p, e, "FAIL not ");
	p = fmtstr(p, e, opname(op));

	FMTENL(p, e);

	writeall(STDERR, buf, p - buf);

	return -1;
}

#define TEST(op, a, b) \
	ret |= test(__FILE__, __LINE__, op, a, b)

int main(void)
{
	int ret = 0;

	TEST(EQ, "abc",  "abc");
	TEST(LT, "abc",  "def");
	TEST(GT, "def",  "abc");

	TEST(LT, "abcd", "abce");
	TEST(GT, "abce", "abcd");

	TEST(LT, "a", "ab");
	TEST(GT, "ab", "a");

	TEST(GT, "\xEF", "\xEE");
	TEST(LT, "\xEE", "\xEF");

	TEST(LT, NULL, "a");
	TEST(GT, "a", NULL);
	TEST(LT, NULL, "\xEE");
	TEST(GT, "\xEE", NULL);
	TEST(EQ, NULL, NULL);

	return ret;
}
