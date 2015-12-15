#include "strlen.h"
#include "strapp.h"

int sumlen(int argc, char** argv)
{
	int i, len = 0;

	for(i = 0; i < argc; i++)
		len += strlen(argv[i]);

	return len;
};

void mergeargs(char* buf, int len, int argc, char** argv)
{
	char* end = buf + len - 1;
	char* p = buf;
	int i;

	for(i = 0; i < argc; i++) {
		p = strapp(p, end, argv[i]);
		if(p >= end) break;
		*p++ = ' ';
	}
}

