#include "strlen.h"
#include "fmtstr.h"

int argsumlen(int argc, char** argv)
{
	int i, len = 0;

	for(i = 0; i < argc; i++)
		len += strlen(argv[i]);

	return len;
};

char* argsmerge(char* buf, char* end, int argc, char** argv)
{
	char* p = buf;
	int i;

	for(i = 0; i < argc && p < end; i++) {
		if(i) *p++ = ' ';
		p = fmtstr(p, end, argv[i]);
	}

	return p;
}
