#include <sys/dents.h>
#include <printf.h>

#define TEST(ret, str) test(__FILE__, __LINE__, str, ret, dotddot(str))

static void test(char* file, int line, char* str, int exp, int got)
{
	if(!!got == !!exp)
		printf("%s:%i OK %i %s\n", file, line, got, str);
	else
		printf("%s:%i FAIL %i %s (expected %i)\n", file, line, got, str, exp);
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
