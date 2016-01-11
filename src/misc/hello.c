#include <sys/write.h>

int main(void)
{
	const char* hello = "Hello, world!\n";
	syswrite(1, hello, 14);
	return 0;
}
