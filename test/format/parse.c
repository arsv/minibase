#include <sys/file.h>
#include <printf.h>
#include <format.h>
#include <string.h>

int test(char* file, int line, char* str, char* ret, int match)
{
	if(!ret || *ret)
		match = 0;

	if(match)
		printf("%s:%i: OK\n", file, line);
	else
		printf("%s:%i: FAIL\n", file, line);

	return !match;
}

#define TEST(type, func, string, expected) {\
	type T; \
	char* r = func(string, &T); \
	ret |= test(__FILE__, __LINE__, string, r, T == expected); \
}

int main(void)
{
	int ret = 0;

	TEST(int, parseint, "1234", 1234);
	//TEST(int, parseint, "-123", -123); /* should be called parseuint() */
	TEST(int, parseint, "1234567890", 1234567890);

	return ret;
}
