#include <sys/file.h>
#include <sys/inotify.h>

#include <printf.h>
#include <main.h>
#include <util.h>

ERRTAG("inotify");

int main(int argc, char** argv)
{
	if(argc < 2)
		fail("too few arguments", NULL, 0);
	if(argc > 2)
		fail("too many arguments", NULL, 0);

	char* dir = argv[1];
	int mask = IN_CREATE;
	int fd, wd;

	if((fd = sys_inotify_init()) < 0)
		fail("inotify-init", NULL, fd);
	if((wd = sys_inotify_add_watch(fd, dir, mask)) < 0)
		fail("inotify-add-watch", NULL, wd);

	char buf[1024];
	int rd;

	while((rd = sys_read(fd, buf, sizeof(buf))) > 0) {
		void* p = buf;
		void* e = buf + rd;

		while(p < e) {
			struct inotify_event* ev = p;
			p += sizeof(*ev) + ev->len;

			if(!ev->len) continue;

			printf("CREATE %s\n", ev->name);
		}

	} if(rd < 0) {
		fail("read", "inotify", rd);
	}

	return 0;
}
