#include <syscall.h>
#include <bits/ints.h>

#define RLIMIT_CPU          0
#define RLIMIT_FSIZE        1
#define RLIMIT_DATA         2
#define RLIMIT_STACK        3
#define RLIMIT_CORE         4
#define RLIMIT_RSS          5
#define RLIMIT_NPROC        6
#define RLIMIT_NOFILE       7
#define RLIMIT_MEMLOCK      8
#define RLIMIT_AS           9
#define RLIMIT_LOCKS       10
#define RLIMIT_SIGPENDING  11
#define RLIMIT_MSGQUEUE    12
#define RLIMIT_NICE        13
#define RLIMIT_RTPRIO      14
#define RLIMIT_RTTIME      15
#define RLIM_NLIMITS       16

struct rlimit {
	uint64_t cur;
	uint64_t max;
};

inline static long sys_prlimit(int pid, int key, const struct rlimit* set,
                                                       struct rlimit* get)
{
	return syscall4(NR_prlimit64, pid, key, (long)set, (long)get);
}
