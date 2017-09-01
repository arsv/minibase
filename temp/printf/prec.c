#include <printf.h>

int main(void)
{
	char str[] = "abcdef";

	printf(">%.3s<\n", str);
	printf(">%.*s<\n", 3, str);
	printf(">%10.3s<\n", str);
	printf(">%-10.3s<\n", str);

	return 0;
}
