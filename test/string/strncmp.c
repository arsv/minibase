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

static int test(char* file, int line, int op, char* a, char* b, int n)
{
	int res = strncmp(a, b, n);

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

#define TEST(op, a, b, n) \
	ret |= test(__FILE__, __LINE__, op, a, b, n)

int main(void)
{
	int ret = 0;

	TEST(EQ, "abc",  "abc",  3);
	TEST(LT, "abc",  "def",  3);
	TEST(EQ, "abce", "abc",  3);
	TEST(LT, "abce", "abcd", 3);
	TEST(GT, "abce", "abcd", 4);

	TEST(GT, "abc",  "a",    4);
	TEST(GT, "abce", "a",    4);

	TEST(EQ, "",  "",  0);
	TEST(EQ, "",  "",  1);
	TEST(LT, "a", "b", 0);
	TEST(GT, "c", "b", 1);
	TEST(EQ, "a", "a", 1);

	TEST(LT, NULL, "a", 1);
	TEST(GT, "a", NULL, 1);
	TEST(EQ, NULL, NULL, 1);
	TEST(LT, NULL, "\xEE", 1);
	TEST(GT, "\xEE", NULL, 1);

	return ret;
}
