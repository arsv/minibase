#include <sys/epoll.h>
#include <sys/file.h>

#include <main.h>
#include <util.h>
#include <printf.h>

ERRTAG("epoll");

int main(int argc, char** argv)
{
	int epfd, ret;
	struct epoll_event ev;
	char buf[128];

	if((epfd = sys_epoll_create()) < 0)
		fail("epoll", NULL, epfd);

	ev.events = EPOLLIN;
	ev.data.fd = 3;

	if((ret = sys_epoll_ctl(epfd, EPOLL_CTL_ADD, 0, &ev)) < 0)
		fail("epoll_ctl", NULL, ret);

	while(1) {
		if((ret = sys_epoll_wait(epfd, &ev, 1, 10000)) < 0)
			fail("epoll_wait", NULL, ret);
		if(ret == 0) {
			warn("timeout", NULL, 0);
			continue;
		} else {
			tracef("ev.data=%i\n", ev.data.fd);
			(void)sys_read(0, buf, sizeof(buf));
		}
	}

	return 0;
}
