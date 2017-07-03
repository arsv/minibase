#include <syscall.h>
#include <bits/time.h>

#define RUSAGE_SELF     0
#define RUSAGE_CHILDREN (-1)
#define RUSAGE_THREAD   1

struct rusage {
	struct timeval utime;
	struct timeval stime;
	long maxrss;
	long ixrss;
	long idrss;
	long isrss;
	long minflt;
	long majflt;
	long nswap;
	long inblock;
	long oublock;
	long msgsnd;
	long msgrcv;
	long nsignals;
	long nvcsw;
	long nivcsw;
};

inline static long getrusage(int who, struct rusage* usage)
{
	return syscall2(__NR_getrusage, who, (long)usage);
}
