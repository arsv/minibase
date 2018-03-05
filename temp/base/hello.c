#include <sys/file.h>

int main(void)
{
	const char msg[] = "Hello, world!\n";
	int len = sizeof(msg) - 1;

	sys_write(STDOUT, msg, len);

	return 0;
}
