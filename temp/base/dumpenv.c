#include <printf.h>

int main(int argc, char** argv, char** envp)
{
	(void)argc;
	(void)argv;

	char** p;

	for(p = envp; *p; p++)
		tracef("[%li] %s\n", p - envp, *p);

	return 0;
}
