#include <format.h>
#include <string.h>
#include <util.h>

static int test(char* file, int line, char* str, char* end)
{
	char* res = strpend(str);

	if(res == end)
		return 0;

	FMTBUF(p, e, buf, 200);

	p = fmtstr(p, e, file);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, line);
	p = fmtstr(p, e, ": ");
	p = fmtstr(p, e, "FAIL");

	FMTENL(p, e);

	writeall(STDERR, buf, p - buf);

	return -1;
}

#define TEST(str, end) \
	ret |= test(__FILE__, __LINE__, str, end)

int main(void)
{
	int ret = 0;
	char* s1 = "abc";
	char* s2 = "a";
	char* sn = "";

	TEST(s1, s1 + 3);
	TEST(s2, s2 + 1);
	TEST(sn, sn + 0);
	TEST(NULL, NULL);

	return ret;
}
