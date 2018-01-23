#include <bits/types.h>
#include <printf.h>
#include <string.h>
#include <format.h>
#include <util.h>

static void failure(char* file, int line, char* exp, char* got)
{
	FMTBUF(p, e, buf, 200);

	p = fmtstr(p, e, file);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, line);
	p = fmtstr(p, e, ":");
	p = fmtstr(p, e, " FAIL \"");
	p = fmtstr(p, e, got);
	p = fmtstr(p, e, "\" expected \"");
	p = fmtstr(p, e, exp);
	p = fmtstr(p, e, "\"");

	FMTENL(p, e);

	writeall(STDERR, buf, p - buf);
}

static void success(char* file, int line, char* got)
{
	FMTBUF(p, e, buf, 200);

	p = fmtstr(p, e, file);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, line);
	p = fmtstr(p, e, ":");
	p = fmtstr(p, e, " OK \"");
	p = fmtstr(p, e, got);
	p = fmtstr(p, e, "\"");

	FMTENL(p, e);

	writeall(STDERR, buf, p - buf);
}

#define TEST(exp, fmt, ...) { \
	char buf[60]; \
	int len = sizeof(buf); \
	int ret; \
	\
	snprintf(buf, len, fmt, __VA_ARGS__); \
	\
	if((ret = strncmp(exp, buf, len))) \
		failure(__FILE__, __LINE__, exp, buf); \
	else \
		success(__FILE__, __LINE__, buf); \
	\
	result |= ret; \
}

int main(void)
{
	int result = 0;

	TEST("int=20", "int=%i", 20);
	TEST("int=12345", "int=%i", 12345);
	TEST("str=hello", "str=%s", "hello");
	TEST("i1i2i3sFOO", "i%ii%ii%is%s", 1, 2, 3, "FOO");
	TEST("i1i2i3sFOOi4", "i%ii%ii%is%si%i", 1, 2, 3, "FOO", 4);

	return result;
}
