#include <util.h>
#include <main.h>

ERRTAG("warn");
ERRLIST(NEPERM);

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;

	warn("W1", NULL, 0);
	warn("W2", "arg", 0);
	warn("W3", NULL, -1);
	warn("W4", "arg", -1);
	warn("Wx", NULL, -100);

	return 0;
}
