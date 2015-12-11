#include <sys/write.h>
#include <strlen.h>
#include <memcpy.h>

/* This merely outputs argv[] contents, to check it *and*
   to make sure argv[] is terminated with a NULL ptr. */

static void writeline(const char* s)
{
	int l = strlen(s);
	char buf[l + 2];
	memcpy(buf, s, l);
	buf[l] = '\n';
	syswrite(1, buf, l+1);
}

int main(int argc, char** argv)
{
	char** p;

	for(p = argv; *p; p++)
		writeline(*p);

	return 0;
}
