#include <sys/file.h>
#include <sys/dents.h>

#include <errtag.h>
#include <string.h>
#include <util.h>

ERRTAG("getdents");

int main(void)
{
	int fd, rd;
	char buf[1024];
	int seen = 0;
	char* self = "getdents.c";

	if((fd = sys_open(".", O_DIRECTORY)) < 0)
		fail("open", ".", fd);

	while((rd = sys_getdents(fd, buf, sizeof(buf))) > 0) {
		void* ptr = buf;
		void* end = buf + rd;

		while(ptr < end) {
			struct dirent* de = ptr;

			if(!de->reclen)
				fail("reclen is zero", NULL, 0);

			ptr += de->reclen;

			if(!strcmp(de->name, self))
				seen++;
		}

	} if(rd < 0) {
		fail(NULL, NULL, rd);
	}

	if(!seen)
		fail("no dirents named", self, 0);
	else if(seen > 1)
		fail("multiple dirents named", self, 0);

	return 0;
}
