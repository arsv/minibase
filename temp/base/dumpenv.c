#include <printf.h>

int main(int argc, char** argv, char** envp)
{
	char** p;

	for(p = envp; *p; p++)
		tracef("[%i] %s\n", p - envp, *p);

	return 0;
}
