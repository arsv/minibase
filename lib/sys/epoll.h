#include <bits/types.h>
#include <bits/signal.h>
#include <syscall.h>

#define EPOLLIN        (1<<0)
#define EPOLLPRI       (1<<1)
#define EPOLLOUT       (1<<2)
#define EPOLLERR       (1<<3)
#define EPOLLHUP       (1<<4)
#define EPOLLNVAL      (1<<5)
#define EPOLLRDNORM    (1<<6)
#define EPOLLRDBAND    (1<<7)
#define EPOLLWRNORM    (1<<8)
#define EPOLLWRBAND    (1<<9)
#define EPOLLMSG       (1<<10)
#define EPOLLRDHUP     (1<<13)
#define EPOLLEXCLUSIVE (1<<28)
#define EPOLLWAKEUP    (1<<29)
#define EPOLLONESHOT   (1<<30)
#define EPOLLET        (1<<31)

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

struct epoll_event {
	uint32_t events;
	union {
		void *ptr;
		int fd;
		uint32_t u32;
		uint64_t u64;
	} data;
} __attribute__((packed));

static inline int sys_epoll_create(void)
{
	return syscall1(NR_epoll_create, 1);
}

static inline int sys_epoll_create1(int flags)
{
	return syscall1(NR_epoll_create1, flags);
}

static inline int sys_epoll_ctl(int epfd, int op, int fd, struct epoll_event* ev)
{
	return syscall4(NR_epoll_ctl, epfd, op, fd, (long)ev);
}

static inline int sys_epoll_wait(int epfd, struct epoll_event* out, int cnt, int tmo)
{
	return syscall4(NR_epoll_wait, epfd, (long)out, cnt, tmo);
}

static inline int sys_epoll_pwait(int epfd, struct epoll_event* out, int cnt, int tmo,
                const struct sigset* mask)
{
	const int ssz = sizeof(struct sigset);
	return syscall6(NR_epoll_pwait, epfd, (long)out, cnt, tmo,
	                (long)mask, ssz);
}
