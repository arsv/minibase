#include <printf.h>
#include <string.h>

#define EQ 0
#define LT -1
#define GT  1

#define TEST(a, c, b) test(__FILE__, __LINE__, a, c, b)

static char* opname(int op)
{
	if(op < 0)
		return "LT";
	if(op > 0)
		return "GT";
	return "EQ";
}

static int normalize(int x)
{
	if(x < 0)
		return -1;
	if(x > 0)
		return 1;
	return 0;
}

static void test(char* file, int line, char* a, int exp, char* b)
{
	int ret = normalize(natcmp(a, b));

	if(ret == exp)
		return;

	tracef("%s:%i: FAIL \"%s\" %s \"%s\" got %s\n",
			file, line, a, opname(exp), b, opname(ret));
}

int main(void)
{
	TEST("", EQ, "");
	TEST("", LT, "a");
	TEST("a", GT, "");

	TEST("abc", EQ, "abc");
	TEST("abc", LT, "def");

	TEST("ab", LT, "abc");
	TEST("abc", GT, "ab");

	TEST("a2", GT, "a1");
	TEST("a10", GT, "a7");
	TEST("a10", EQ, "a10");
	TEST("a10", GT, "a1");
	TEST("a10", LT, "a11");

	TEST("ab", LT, "ab1");
	TEST("ab!", LT, "ab1");
	TEST("abc", GT, "ab1");

	return 0;
}
