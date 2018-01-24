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

#define TNULL(str) \
	if((p = parseip(str, ip))) \
		failf("%s:%i: FAIL %s parsed\n", FL, str);
#define TLEFT(str, left) \
	if(!(p = parseip(str, ip))) \
		failf("%s:%i: FAIL %s NULL\n", FL, str); \
	if(strcmp(p, left)) \
		failf("%s:%i: FAIL %s :: %s\n", FL, str, p);

#define TOK(str, a, b, c, d) {\
	byte exp[] = { a, b, c, d }; \
	if(!(p = parseip(str, ip)) || *p) \
		failf("%s:%i: FAIL %s not parsed\n", FL, str);\
	if(memcmp(ip, exp, 4)) \
		failf("%s:%i: FAIL %s -> %i.%i.%i.%i\n", FL,\
				str, ip[0], ip[1], ip[2], ip[3]);\
}

int main(void)
{
	byte ip[4];
	char* p;

	TNULL("");
	TNULL("192");
	TNULL("192.");
	TNULL("192.11");
	TNULL("192.168.1.");

	TLEFT("192.168.1.1.1", ".1");
	TLEFT("192.168.1.1+2", "+2");
	TLEFT("192.168.1.1ab", "ab");

	TOK("192.168.1.1", 192, 168, 1, 1);
	TOK("173.194.221.147", 173, 194, 221, 147);

	return 0;
}
