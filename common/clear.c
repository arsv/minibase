#include <sys/write.h>

int main(int argc, char** argv)
{
	const char* clr = "\x1b[2J\x1b[H";
	const int len = 7;

	syswrite(1, clr, len);

	return 0;
}
