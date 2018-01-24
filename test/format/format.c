#include <sys/file.h>
#include <format.h>
#include <string.h>
#include <util.h>

static void invalid(char* file, int line, char* msg)
{
	FMTBUF(p, e, buf, 100);
	p = fmtstr(p, e, file);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, line);
	p = fmtstr(p, e, ":");
	p = fmtstr(p, e, " FAIL ");
	p = fmtstr(p, e, msg);
	FMTENL(p, e);

	writeall(STDERR, buf, p - buf);

	_exit(0xFF);
}

static void failure(char* file, int line, char* got, char* exp)
{
	FMTBUF(p, e, buf, 100);
	p = fmtstr(p, e, file);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, line);
	p = fmtstr(p, e, ":");
	p = fmtstr(p, e, " FAIL ");
	p = fmtstr(p, e, got);
	p = fmtstr(p, e, " expected ");
	p = fmtstr(p, e, exp);
	FMTENL(p, e);

	writeall(STDERR, buf, p - buf);

	_exit(0xFF);
}

#define TEST(func, expected, ...) { \
	memset(buf, '*', sizeof(buf)); \
	char* q = func(p, e, __VA_ARGS__); \
	test(__FILE__, __LINE__, p, e, q, expected); \
}

void test(char* file, int line, char* p, char* e, char* q, char* expected)
{
	if(!q)
		invalid(file, line, "NULL return");
	if(q < p || q > e)
		invalid(file, line, "ptr outside buffer");

	if(strncmp(q, "*****", 5))
		invalid(file, line, "overshot");

	*q = '\0';

	if(strcmp(p, expected))
		failure(file, line, q, expected);
}

int test_basic_types(void)
{
	char buf[256];
	char* p = buf;
	char* e = buf + sizeof(buf) - 5;
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
	char* e = buf + sizeof(buf) - 5;
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
