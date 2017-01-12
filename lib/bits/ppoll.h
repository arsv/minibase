#ifndef __BITS_PPOLL_H__
#define __BITS_PPOLL_H__

#define POLLIN     0x001
#define POLLPRI    0x002
#define POLLOUT    0x004
#define POLLERR    0x008
#define POLLHUP    0x010

struct pollfd {
	int fd;
	short events;
	short revents;
};

#endif
