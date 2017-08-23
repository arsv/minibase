#include <bits/signal.h>
#include <bits/time.h>
#include <syscall.h>

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

inline static long sys_ppoll(struct pollfd* fds, long nfds,
                             const struct timespec* ts, sigset_t* sigmask)
{
	return syscall5(__NR_ppoll, (long)fds, nfds, (long)ts,
                                    (long)sigmask, sizeof(*sigmask));
}
