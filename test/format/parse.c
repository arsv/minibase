#include <sys/file.h>
#include <printf.h>
#include <format.h>
#include <string.h>

#define TNULL(parse, str, type) { \
	type var; \
	if((p = parse(str, &var))) { \
		tracef("%s:%i: FAIL %s parsed\n", __FILE__, __LINE__, str); \
		ret = -1; \
	} else { \
		tracef("%s:%i: OK %s not parsed\n", __FILE__, __LINE__, str); \
	} \
}
#define TLEFT(parse, str, type, left) { \
	type var; \
	if(!(p = parse(str, &var))) {\
		tracef("%s:%i: FAIL %s NULL\n", __FILE__, __LINE__, str); \
		ret = -1; \
	} else if(strcmp(p, left)) {\
		tracef("%s:%i: FAIL %s :: %s\n", __FILE__, __LINE__, str, p); \
		ret = -1; \
	} else { \
		tracef("%s:%i: OK %s left %s\n", __FILE__, __LINE__, str, p); \
	} \
}
#define TOK(parse, str, type, exp, fmt) {\
	type var; \
	if(!(p = parse(str, &var)) || *p) { \
		tracef("%s:%i: FAIL %s not parsed\n", __FILE__, __LINE__, str); \
		ret = -1; \
	} else if(var != exp) { \
		tracef("%s:%i: FAIL %s -> " fmt "\n", __FILE__, __LINE__, str, var); \
		ret = -1; \
	} else { \
		tracef("%s:%i: OK %s -> " fmt "\n", __FILE__, __LINE__, str, var); \
	} \
}

int main(void)
{
	int ret = 0;
	char* p;

	TNULL(parseint, "", int);
	TNULL(parseint, "abc", int);
	TLEFT(parseint, "12ab", int, "ab");

	TOK(parseint, "123", int, 123, "%i");
	TOK(parseint, "1", int, 1, "%i");

	return ret;
}
