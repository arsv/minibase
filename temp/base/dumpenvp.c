#include <main.h>
#include <printf.h>

int main(int argc, char** argv)
{
	char** envp = argv + argc + 1;
	char** p;

	for(p = envp; *p; p++)
		tracef("[%i] %s\n", (int)(p - envp), *p);

	return 0;
}
