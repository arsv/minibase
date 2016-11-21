#include <string.h>
#include <format.h>

/* When starting a command, shell splits its arguments on whitespaces

	cmd some text here -> execve("cmd", [ "some", "text", "here" ])

   Some tools like echo and insmod want their arguments merged back
   into a single string however, "some text here".

   The buffer for the string is obtained via alloca(), so we check how
   much space is needed for the merged string, let the caller prepare
   the buffer, and only then proceed to merge. */

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
