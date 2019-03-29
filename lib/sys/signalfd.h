#include <syscall.h>
#include <bits/types.h>
#include <bits/fcntl.h>
#include <bits/signal.h>

#define SFD_NONBLOCK O_NONBLOCK
#define SFD_CLOEXEC  O_CLOEXEC

struct siginfo {
	uint32_t signo;
	 int32_t errno;
	 int32_t code;
	uint32_t pid;
	uint32_t uid;
	 int32_t fd;
	uint32_t tid;
	uint32_t band;
	uint32_t overrun;
	uint32_t trapno;
	 int32_t status;
	 int32_t intval;
	uint64_t ptrval;
	uint64_t utime;
	uint64_t stime;
	uint64_t addr;
	uint16_t addr_lsb;
	byte pad[46];
} __attribute__((packed));

inline static long sys_signalfd(int fd, sigset_t* set, int flags)
{
	return syscall4(NR_signalfd4, fd, (long)set, sizeof(*set), flags);
}
