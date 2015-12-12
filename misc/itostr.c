#include <sys/write.h>
#include <atoi.h>
#include <itostr.h>

//char* itostr(char* buf, char* end, int num)

void dump(int i)
{
	char buf[30];
	char* end = buf + sizeof(buf);

	char* p = itostr(buf, end, i);
	*p++ = '\n';

	syswrite(1, buf, p - buf);
}

int main(int argc, char** argv)
{
	int i;

	for(i = 1; i < argc; i++)
		dump(atoi(argv[i]));

	return 0;
}
