#include <printf.h>

int main(void)
{
	printf("i %i\n", 1234);
	printf("s %s\n", "some string here");
	printf("x %x\n", 0xABCD);
	printf("li %li\n", 12345678l);
	printf("lx %lx\n", 0xABCDEFlu);
	printf("p %p\n", &main);

	return 0;
}
