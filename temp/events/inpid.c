#include <bits/input.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <format.h>
#include <string.h>
#include <util.h>
#include <fail.h>

ERRTAG = "inpid";
ERRLIST = { RESTASNUMBERS };

static void list_keys(int fd)
{
	char data[32+1];
	int datasize = sizeof(data);

	memset(data, 0, datasize);

	if(sys_ioctl(fd, EVIOCGNAME(datasize), data) < 0)
		return;

	data[32] = '\0';
	int n = strlen(data);
	data[n] = '\n';

	sys_write(STDOUT, data, n + 1);
}

int main(int argc, char** argv)
{
	int fd;

	if(argc != 2)
		fail("bad call", NULL, 0);
	if((fd = sys_open(argv[1], O_RDONLY)) < 0)
		fail("open", argv[1], fd);

	list_keys(fd);

	return 0;
}
