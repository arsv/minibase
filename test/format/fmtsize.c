#include <sys/file.h>
#include <format.h>
#include <string.h>
#include <util.h>

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

#define TEST(expected, size) { \
	char* q = fmtsize(p, e, size); \
	test(__FILE__, __LINE__, p, q, expected); \
}

void test(char* file, int line, char* got, char* end, char* exp)
{
	*end = '\0';

	if(!strcmp(got, exp))
		return;

	failure(file, line, got, exp);
}

#define KB 1024
#define MB KB*1024
#define GB MB*1024
#define TB GB*1024
#define PB TB*1024

int main(void)
{
	char buf[256];
	char* p = buf;
	char* e = buf + sizeof(buf) - 5;

	TEST("0",    0);
	TEST("1",    1);
	TEST("1023", 1023);

	TEST("1K",   1024);
	TEST("1K",   1024 + 511);
	TEST("2K",   1024 + 512);
	TEST("2K",   2047);
	TEST("2K",   2048);

	TEST("1.0M", MB);
	TEST("1.0M", MB + 51*KB);
	TEST("1.1M", MB + 52*KB);
	TEST("1.1M", MB + 153*KB);
	TEST("1.2M", MB + 154*KB);
	TEST("2.0M", MB + 1023*KB);
	TEST("2.0M", MB + 1024*KB);

	TEST("1.0G", GB);
	TEST("1.5G", GB + 512*MB);

	TEST("15G", 15ULL*GB + 500*MB);
	TEST("16G", 15ULL*GB + 600*MB);
	TEST("150G", 150ULL*GB + 500*MB);

	TEST("100P", 100ULL*PB + 104ULL*TB);
	/* overflow */
	TEST("2000P", 2000ULL*PB);
	/* overflow with rounding carry */
	TEST("2001P", 2000ULL*PB + 512ULL*TB);

	return 0;
}
