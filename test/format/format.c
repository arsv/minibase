#include <sys/file.h>
#include <printf.h>
#include <format.h>
#include <string.h>

static int invalid(char* file, int line, char* msg)
{
	printf("%s:%i: FAIL %s\n", file, line, msg);
	return -1;
}

static int failure(char* file, int line, char* got, char* exp)
{
	printf("%s:%i: FAIL %s expected %s\n", file, line, got, exp);
	return -1;
}

static int success(char* file, int line, char* got)
{
	printf("%s:%i: OK %s\n", file, line, got);
	return 0;
}

#define TEST(func, expected, ...) { \
	memset(buf, '*', sizeof(buf)); \
	char* q = func(p, e, __VA_ARGS__); \
	ret |= test(__FILE__, __LINE__, p, e, q, expected); \
}

int test(char* file, int line, char* p, char* e, char* q, char* expected)
{
	if(!q)
		return invalid(file, line, "NULL return");
	if(q < p || q > e)
		return invalid(file, line, "ptr outside buffer");

	if(strncmp(q, "*****", 5))
		return invalid(file, line, "overshot");

	*q = '\0';

	if(strcmp(p, expected))
		return failure(file, line, q, expected);
	
	return success(file, line, p);
}

int test_basic_types(void)
{
	char buf[256];
	char* p = buf;
	char* e = buf + sizeof(buf);
	int ret = 0;

	TEST(fmtbyte, "20", 0x20);
	TEST(fmtbyte, "8C", 0x8C);

	TEST(fmtint, "123", 123);
	TEST(fmtint, "1234567890", 1234567890);
	TEST(fmtint, "-10", -10);

	TEST(fmtxlong, "ABCD", 0xABCD);
	TEST(fmtxlong, "FFFFFFFF", 0xFFFFFFFF);

	TEST(fmtchar, "Q", 'Q');

	TEST(fmtstr, "foo", "foo");
	TEST(fmtstrn, "abc", "abc", 4);
	TEST(fmtstrn, "abc", "abc", 3);
	TEST(fmtstrn, "ab", "abc", 2);

	TEST(fmtraw, "abc", "abcdef", 3);

	return ret;
}

/* Check clipping in buffers too short to fit the output. */

int test_buf_cliping(void)
{
	char buf[10];
	char* p = buf;
	char* e = buf + 3;
	int ret = 0;

	TEST(fmtint, "123", 1234);
	TEST(fmtstr, "abc", "abcdef");
	TEST(fmtxlong, "ABC", 0xABCDEF);

	return ret;
}

int test_padding(void)
{
	char buf[256];
	char* p = buf;
	char* e = buf + sizeof(buf);
	int ret = 0;

	TEST(fmtpad,  "  a", 3, fmtstr(p, e, "a"));
	TEST(fmtpad0, "00a", 3, fmtstr(p, e, "a"));

	TEST(fmtpad,   "abc", 3, fmtstr(p, e, "abc"));
	TEST(fmtpad0,  "abc", 3, fmtstr(p, e, "abc"));

	TEST(fmtpad,   "abc", 1, fmtstr(p, e, "abc"));
	TEST(fmtpad0,  "abc", 1, fmtstr(p, e, "abc"));

	return ret;
}

int main(void)
{
	int ret = 0;

	ret |= test_basic_types();
	//ret |= test_buf_cliping();
	//ret |= test_padding();

	return ret;
}
