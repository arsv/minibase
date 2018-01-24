#include <printf.h>

int main(int argc, char** argv, char** envp)
{
	char** p;

	(void)argc;
	(void)argv;

	for(p = envp; *p; p++)
		tracef("[%li] %s\n", p - envp, *p);

	return 0;
}
