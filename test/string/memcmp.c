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

static int test(char* file, int line, int op, void* a, void* b, int n)
{
	int res = memcmp(a, b, n);

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

	TEST(EQ, "abc",  "abc", 3);
	TEST(LT, "abc",  "def", 3);
	TEST(GT, "def",  "abc", 3);

	TEST(GT, "\xFF",  "\x00", 1);

	return ret;
}
