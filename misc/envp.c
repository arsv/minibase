#include <sys/write.h>
#include <strlen.h>

void dump(char* str)
{
	int len = strlen(str);
	str[len++] = '\n';
	syswrite(1, str, len);
}

int main(int argc, char** argv, char** envp)
{
	char** p;

	for(p = envp; *p; p++)
		dump(*p);

	return 0;
}
