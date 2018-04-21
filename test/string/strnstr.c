#include <format.h>
#include <string.h>
#include <util.h>

static void test(int* ret, char* file, int line, char* exp, char* got)
{
	FMTBUF(p, e, buf, 200);

	p = fmtstr(p, e, file);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, line);
	p = fmtstr(p, e, ": ");

	if(!exp && !got) {
		p = fmtstr(p, e, "OK got NULL");
	} else if(exp && !got) {
		p = fmtstr(p, e, "FAIL got NULL");
		*ret = -1;
	} else if(!exp && got) {
		p = fmtstr(p, e, "FAIL got \"");
		p = fmtstr(p, e, got);
		p = fmtstr(p, e, "\" not NULL");
		*ret = -1;
	} else if(!strcmp(got, exp)) {
		p = fmtstr(p, e, "OK got \"");
		p = fmtstr(p, e, got);
		p = fmtstr(p, e, "\"");
	} else {
		p = fmtstr(p, e, "FAIL got \"");
		p = fmtstr(p, e, got);
		p = fmtstr(p, e, "\" not \"");
		p = fmtstr(p, e, exp);
		p = fmtstr(p, e, "\"");
		*ret = -1;
	}

	FMTENL(p, e);

	writeall(STDERR, buf, p - buf);
}

#define TEST(big, little, len, exp) \
	test(&ret, __FILE__, __LINE__, exp, strnstr(big, little, len))

int main(void)
{
	int ret = 0;

	TEST("mc", "mc", 2, "mc");
	TEST("zzmc", "mc", 2, NULL);
	TEST("zzmc", "mc", 4, "mc");
	TEST("zzmcxx", "mc", 4, "mcxx");

	return ret;
}
