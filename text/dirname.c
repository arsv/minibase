#include <sys/write.h>
#include <strlen.h>

void dumpdname(char* path)
{
	int len = strlen(path);
	char* p = path + len - 1;

	while(p > path)
		if(*p == '/')
			break;
		else
			p--;

	while(p > path && *(p-1) == '/')
		p--;

	int wr;
	if(p > path) {
		*p = '\n';
		wr = p - path + 1;
	} else if(*p != '/') {
		path = ".\n";
		wr = 2;
	} else {
		path[len] = '\n';
		wr = len + 1;
	}

	syswrite(1, path, wr);
}

int main(int argc, char** argv)
{
	int i;

	for(i = 1; i < argc; i++)
		dumpdname(argv[i]);

	return 0;
}
