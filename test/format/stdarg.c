#include <stdarg.h>
#include <format.h>
#include <util.h>

static void check(int exp, int got)
{
	if(exp == got)
		return;

	FMTBUF(p, e, buf, 100);

	p = fmtstr(p, e, __FILE__);
	p = fmtstr(p, e, ":");

	p = fmtstr(p, e, " FAIL ");
	p = fmtint(p, e, got);
	p = fmtstr(p, e, " expected ");
	p = fmtint(p, e, exp);

	FMTENL(p, e);

	writeall(STDERR, buf, p - buf);

	_exit(0xFF);
}

static void ints(const char* fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);

	check(1, va_arg(ap, int));
	check(2, va_arg(ap, int));
	check(3, va_arg(ap, int));
	check(4, va_arg(ap, int));
	check(5, va_arg(ap, int));
	check(6, va_arg(ap, int));

	va_end(ap);
}

int main(void)
{
	ints("foo", 1, 2, 3, 4, 5, 6);

	return 0;
}
