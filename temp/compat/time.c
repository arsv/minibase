#include <sys/proc.h>
#include <sys/time.h>
#include <sys/rusage.h>

#include <format.h>
#include <output.h>
#include <util.h>
#include <main.h>

ERRTAG("time");
ERRLIST(NEINVAL NEFAULT NEAGAIN NENOMEM NENOSYS NECHILD NEINTR NENOENT);

static void spawn(char** argv, char** envp)
{
	int ret;

	if((ret = execvpe(*argv, argv, envp)) < 0)
		fail("exec", *argv, ret);

	_exit(0);
}

static void tvdiff(struct timeval* tv, struct timeval* t1, struct timeval* t0)
{
	time_t ds = t1->sec - t0->sec;
	long dus = t1->usec - t0->usec;

	if(dus < 0) {
		dus += 1000000;
		ds -= 1;
	}

	tv->sec = ds;
	tv->usec = dus;
}

static char* fmtint0(char* p, char* e, int n, int w)
{
	return fmtpad0(p, e, w, fmti32(p, e, n));
}

static char* fmt_tv(char* p, char* e, struct timeval* tv)
{
	time_t ts = tv->sec;
	time_t us = tv->usec;

	int cs = tv->usec / 10000; /* centiseconds */
	int sec = ts % 60; ts /= 60;
	int min = ts % 60; ts /= 60;
	int hor = ts % 24; ts /= 24;
	int days = ts;

	ts = tv->sec;

	if(ts >= 24*60*60) {
		p = fmtint(p, e, days);
		p = fmtstr(p, e, "d");
	} if(ts >= 60*60) {
		p = fmtint0(p, e, hor, 2);
		p = fmtstr(p, e, ":");
	} if(ts >= 60) {
		p = fmtint0(p, e, min, 2);
		p = fmtstr(p, e, ":");
		p = fmtint0(p, e, sec, 2);
	} else if(ts > 0) {
		p = fmtint(p, e, sec);
		p = fmtstr(p, e, ".");
		p = fmtint0(p, e, cs, 2);
	} else if(us >= 1000) {
		p = fmtint(p, e, us/1000);
		p = fmtstr(p, e, "ms");
	} else if(us > 0) {
		p = fmtint(p, e, us);
		p = fmtstr(p, e, "us");
	} else {
		p = fmtstr(p, e, "0");
	}

	return p;
}

static void report(struct rusage* rv, struct timeval* tv)
{
	char buf[500];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = fmtstr(p, e, errtag);
	p = fmtstr(p, e, ": ");
	p = fmtstr(p, e, "real ");
	p = fmt_tv(p, e, tv);
	p = fmtstr(p, e, " user ");
	p = fmt_tv(p, e, &rv->utime);
	p = fmtstr(p, e, " sys ");
	p = fmt_tv(p, e, &rv->stime);
	*p++ = '\n';

	writeall(STDERR, buf, p - buf);
}

/* Measuring time intervals requires CLOCK_MONOTONIC which in turn means
   clock_gettime and timespec (ns). But struct rusage returns timevals (us),
   so for simplicity ns values get truncated to us. */

static void note_time(struct timeval* tv)
{
	struct timespec ts;
	int ret;

	if((ret = sys_clock_gettime(CLOCK_MONOTONIC, &ts)) < 0)
		fail("clock_gettime", NULL, ret);

	tv->sec = ts.sec;
	tv->usec = ts.nsec / 1000;
}

int main(int argc, char** argv)
{
	char** envp = argv + argc + 1;
	int i = 1;
	struct timeval t0, t1, tv;
	struct rusage rv;
	int pid, ret, status;

	if(i < argc && argv[i][0] == '-')
		if(argv[i++][1])
			fail("unsupported options", NULL, 0);
	if(i >= argc)
		fail("too few arguments", NULL, 0);

	note_time(&t0);

	if((pid = sys_fork()) < 0)
		fail("fork", NULL, pid);
	if(pid == 0)
		spawn(argv + i, envp);
	if((ret = sys_waitpid(pid, &status, 0)) < 0)
		fail("wait", 0, ret);

	note_time(&t1);

	if((ret = getrusage(RUSAGE_CHILDREN, &rv)) < 0)
		fail("getrusage", NULL, ret);

	tvdiff(&tv, &t1, &t0);
	report(&rv, &tv);

	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
