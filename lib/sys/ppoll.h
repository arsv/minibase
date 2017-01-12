#include <syscall.h>
#include <bits/time.h>
#include <bits/signal.h>
#include <bits/ppoll.h>
#include <bits/stat.h>

inline static long sysppoll(struct pollfd* fds, long nfds,
		const struct timespec* ts, sigset_t* sigmask)
{
	return syscall5(__NR_ppoll, (long)fds, nfds, (long)ts,
			(long)sigmask, sizeof(*sigmask));
}
