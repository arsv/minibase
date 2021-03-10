#include <util.h>
#include <main.h>

ERRTAG("execvp");

int main(int argc, char** argv)
{
	int ret;
	char** envp = argv + argc + 1;
	ret = execvpe("ls", argv, envp);
	fail(NULL, NULL, ret);
}
