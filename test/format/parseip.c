#include <printf.h>
#include <format.h>
#include <string.h>

#define TNULL(str) { \
	if((p = parseip(str, ip))) { \
		tracef("%s:%i: FAIL %s parsed\n", __FILE__, __LINE__, str); \
		ret = -1; \
	} else { \
		tracef("%s:%i: OK %s not parsed\n", __FILE__, __LINE__, str); \
	} \
}
#define TLEFT(str, left) { \
	if(!(p = parseip(str, ip))) {\
		tracef("%s:%i: FAIL %s NULL\n", __FILE__, __LINE__, str); \
		ret = -1; \
	} else if(strcmp(p, left)) {\
		tracef("%s:%i: FAIL %s :: %s\n", __FILE__, __LINE__, str, p); \
		ret = -1; \
	} else { \
		tracef("%s:%i: OK %s left %s\n", __FILE__, __LINE__, str, p); \
	} \
}
#define TOK(str, a, b, c, d) {\
	byte exp[] = { a, b, c, d }; \
	if(!(p = parseip(str, ip)) || *p) { \
		tracef("%s:%i: FAIL %s not parsed\n", __FILE__, __LINE__, str);\
		ret = -1; \
	} else if(memcmp(ip, exp, 4)) { \
		tracef("%s:%i: FAIL %s -> %i.%i.%i.%i\n", __FILE__, __LINE__,\
				str, ip[0], ip[1], ip[2], ip[3]);\
		ret = -1; \
	} else { \
		tracef("%s:%i: OK %s -> %i.%i.%i.%i\n", __FILE__, __LINE__,\
				str, ip[0], ip[1], ip[2], ip[3]);\
	} \
}

int main(void)
{
	int ret = 0;
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

	return ret;
}
