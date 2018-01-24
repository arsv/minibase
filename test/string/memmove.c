#include <sys/file.h>
#include <format.h>
#include <string.h>

static char* fmt_tag(char* p, char* e, int line)
{
	p = fmtstr(p, e, __FILE__);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, line);
	p = fmtstr(p, e, ":");
	p = fmtstr(p, e, " ");
	return p;
}

static void report(int line, char* tag, char* val)
{
	char buf[200];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = fmt_tag(p, e, line);
	p = fmtstr(p, e, tag);
	p = fmtstr(p, e, " ");
	p = fmtstr(p, e, val);

	*p++ = '\n';

	sys_write(STDOUT, buf, p - buf);
}

void test(char* expected, char* actual, int len, int line)
{
	if(!memcmp(expected, actual, len))
		return;

	report(line, "FAIL", expected);
	report(line, " got", actual);
}

int main(void)
{
	/*             0         1         2    */
	/*             012345678901234567890123 */
	char orig[] = "||-1111-AA-123456-444-||";
	char mov1[] = "||-1111-AA-1-AA-6-444-||";
	char mov2[] = "||--AA--AA-123456-444-||";
	char mov3[] = "||-1111-AA-234566-444-||";
	char mov4[] = "||-1111-AA-112345-444-||";
	int len = sizeof(orig);
	char work[len];

	memcpy(work, orig, len);
	memmove(work + 12, work + 7, 4);
	test(work, mov1, len, __LINE__);

	memcpy(work, orig, len);
	memmove(work + 3, work + 7, 4);
	test(work, mov2, len, __LINE__);

	memcpy(work, orig, len);
	memmove(work + 11, work + 12, 5);
	test(work, mov3, len, __LINE__);

	memcpy(work, orig, len);
	memmove(work + 12, work + 11, 5);
	test(work, mov4, len, __LINE__);

	return 0;
}
