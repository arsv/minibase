#include <sys/write.h>

#include <alloca.h>
#include <util.h>

/* We do one syscall per line here, instead of making a huge
   buffer and writing it all at once. This is because
   the output will likely be read one line at a time.

   Also this should give the user (marginally) more chances
   to stop the output.

   Code-wise this is echo with all the parsing removed,
   and the loop added. No idea why it needs argsmerge,
   but other implementation do it, so let's be compatible. */

static const char yes[] = "y\n";

int main(int argc, char** argv)
{
	const char* msg;
	int len;

	if(argc > 1) {
		argc--; argv++;

		int alen = argsumlen(argc, argv) + argc;
		char* buf = alloca(alen + 1);
		char* end = argsmerge(buf, buf + alen, argc, argv);
		*end++ = '\n';

		msg = buf;
		len = end - buf;
	} else {
		msg = yes;
		len = sizeof(yes) - 1;
	}

	while(syswrite(1, msg, len) > 0)
		;

	return -1;
}
