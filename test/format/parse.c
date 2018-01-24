#include <sys/file.h>
#include <printf.h>
#include <format.h>
#include <string.h>
#include <util.h>

#define FL __FILE__, __LINE__

__attribute__((format(printf,1,2)))
static void failf(const char* fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfdprintf(STDERR, fmt, ap);
	va_end(ap);

	_exit(0xFF);
}

#define TNULL(parse, str, var) \
	if((p = parse(str, &var))) \
		failf("%s:%i: FAIL %s parsed\n", FL, str);

#define TLEFT(parse, str, var, left) \
	if(!(p = parse(str, &var))) \
		failf("%s:%i: FAIL %s NULL\n", FL, str); \
	if(strcmp(p, left)) \
		failf("%s:%i: FAIL %s :: %s\n", FL, str, p);
#define TOK(parse, str, var, exp, fmt) \
	if(!(p = parse(str, &var)) || *p) \
		failf("%s:%i: FAIL %s not parsed\n", FL, str); \
	if(var != exp) \
		failf("%s:%i: FAIL %s -> " fmt "\n", FL, str, var);

int main(void)
{
	int iv;
	char* p;

	TNULL(parseint, "",    iv);
	TNULL(parseint, "abc", iv);

	TLEFT(parseint, "12ab", iv, "ab");

	TOK(parseint, "123", iv, 123, "%i");
	TOK(parseint, "1",   iv, 1,   "%i");

	return 0;
}
