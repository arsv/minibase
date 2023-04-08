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
	int res = strcmpn(a, b, n);

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

	/* make sure we are not overrunning the LHS */
	TEST(LT, "x",   "x",   0);    /* "" < "x" */
	TEST(LT, "abx", "abx", 2);    /* "ab" < "abx" */

	/* full-length LHS */
	TEST(EQ, "abc",  "abc",  3);  /* "abc" = "abc" */
	TEST(LT, "abc",  "def",  3);  /* "abc" < "def" */
	TEST(LT, "abc",  "abcd", 3);
	TEST(GT, "abd",  "abcd", 3);

	/* short (padded) LHS */
	TEST(LT, "a",    "abc",  3);
	TEST(GT, "ab",   "a",    3);
	TEST(EQ, "ab",   "ab",   3);
	TEST(GT, "c",    "b",    3);

	/* edge cases */
	TEST(EQ, "",  "",  0);
	TEST(EQ, "",  "",  1);

	return ret;
}
