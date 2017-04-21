#include <bits/ints.h>
#include <bits/rlimit.h>
#include <syscall.h>

struct rlimit {
	uint64_t cur;
	uint64_t max;
};

inline static long sys_prlimit(int pid, int key,
                               const struct rlimit* set, struct rlimit* get)
{
	return syscall4(__NR_prlimit64, pid, key, (long)set, (long)get);
}
