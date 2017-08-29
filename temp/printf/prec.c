#include <printf.h>

int main(void)
{
	char str[] = "abcdef";

	printf(">%.3s<\n", str);
	printf(">%.*s<\n", 3, str);

	return 0;
}
