#include <sys/ppoll.h>
#include <cdefs.h>

int uc_wait_writable(int fd)
{
	struct timespec ts = { 1, 0 };
	struct pollfd pfd = { fd, POLLOUT, 0 };
	int ret;

	if((ret = sys_ppoll(&pfd, 1, &ts, NULL)) < 0)
		return ret;
	else if(ret == 0)
		return -ETIMEDOUT;

	return 0;
}
