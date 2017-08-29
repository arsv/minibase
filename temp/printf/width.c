#include <printf.h>

int main(void)
{
	char str[] = "abc";

	printf("R  >%10s<\n", str);
	printf("L  >%-10s<\n", str);
	printf("R* >%*s<\n", 10, str);
	printf("L* >%-*s<\n", 10, str);

	return 0;
}
