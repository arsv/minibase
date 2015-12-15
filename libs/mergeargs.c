#include "strlen.h"
#include "strapp.h"

int sumlen(int argc, char** argv)
{
	int i, len = 0;

	for(i = 0; i < argc; i++)
		len += strlen(argv[i]);

	return len;
};

char* mergeargs(char* buf, char* end, int argc, char** argv)
{
	char* p = buf;
	int i;

	for(i = 0; i < argc && p < end; i++) {
		if(i) *p++ = ' ';
		p = strapp(p, end, argv[i]);
	}

	return p;
}

