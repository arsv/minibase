#include <string.h>
#include <main.h>
#include <util.h>

/* Basic sanity check for a proper main() invocation.
   Should catch some _start.s bugs, likely by segfaulting
   instead of actually reporting anything. */

ERRTAG("_start");

int main(int argc, char** argv)
{
	char** envp = argv + argc + 1;
	char** p;
	int path = 0;
	int i;

	if(argc < 1 || argc > 10)
		fail("bogus argc", NULL, 0);
	if(strcmp(basename(argv[0]), "_start"))
		fail("invalid argv[0]:", argv[0], 0);

	for(i = 0; i < argc; i++)
		if(!argv[i])
			fail("NULLs in argv", NULL, 0);
	if(argv[i])
		fail("no terminating NULL in argv", NULL, 0);

	if(envp != argv + argc + 1)
		fail("invalid envp", NULL, 0);

	for(p = envp; *p; p++)
		if(!strncmp(*p, "PATH=", 5))
			path++;

	if(!path)
		fail("no PATH in envp", NULL, 0);

	return 0;
}
