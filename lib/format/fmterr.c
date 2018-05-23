#include <string.h>
#include <format.h>
#include <main.h>

char* fmterr(char* p, char* e, int err)
{
	const char* q;

	for(q = errlist; *q; q += strlen(q) + 1)
		if(*((unsigned char*) q) == -err)
			return fmtstr(p, e, q + 1);

	return fmtint(p, e, err);
};
