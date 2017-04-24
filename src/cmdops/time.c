#include <sys/fork.h>
#include <sys/waitpid.h>
#include <sys/getrusage.h>
#include <sys/gettimeofday.h>

#include <format.h>
#include <output.h>
#include <util.h>
#include <fail.h>

ERRTAG = "time";
ERRLIST = {
	REPORT(EINVAL), REPORT(EFAULT), REPORT(EAGAIN), REPORT(ENOMEM),
	REPORT(ENOSYS), REPORT(ECHILD), REPORT(EINTR), RESTASNUMBERS
};

extern void _exit(int) __attribute__((noreturn));

static void spawn(char** argv, char** envp)
{
	xchk(execvpe(*argv, argv, envp), "exec", *argv);
	_exit(0);
}

static void tvdiff(struct timeval* tv, struct timeval* t1, struct timeval* t0)
{
	time_t ds = t1->tv_sec - t0->tv_sec;
	long dus = t1->tv_usec - t0->tv_usec;

	if(dus < 0) {
		dus += 1000000;
		ds -= 1;
	}

	tv->tv_sec = ds;
	tv->tv_usec = dus;
}

static char* fmtint0(char* p, char* end, int n, int w)
{
	return fmtpad0(p, end, w, fmti32(p, end, n));
}

static char* fmttv(char* p, char* e, struct timeval* tv)
{
	time_t ts = tv->tv_sec;

	int cs = tv->tv_usec / 10000; /* centiseconds */
	int sec = ts % 60; ts /= 60;
	int min = ts % 60; ts /= 60;
	int hor = ts % 24; ts /= 24;
	int days = ts;

	ts = tv->tv_sec;

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
	} else {
		p = fmtint(p, e, sec);
		p = fmtstr(p, e, ".");
		p = fmtint0(p, e, cs, 2);
	}

	return p;
}

static void report(struct rusage* rv, struct timeval* tv)
{
	char buf[500];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = fmtstr(p, e, "real ");
	p = fmttv(p, e, tv);
	p = fmtstr(p, e, " user ");
	p = fmttv(p, e, &rv->ru_utime);
	p = fmtstr(p, e, " sys ");
	p = fmttv(p, e, &rv->ru_stime);
	*p++ = '\n';

	writeall(STDERR, buf, p - buf);
}

int main(int argc, char** argv, char** envp)
{
	int i = 1;
	struct timeval t0, t1, tv;
	struct rusage rv;

	if(i < argc && argv[i][0] == '-')
		if(argv[i++][1])
			fail("unsupported options", NULL, 0);

	sysgettimeofday(&t0, NULL);

	long pid = xchk(sysfork(), "fork", NULL);

	if(!pid) spawn(argv + i, envp);

	int status;

	xchk(syswaitpid(pid, &status, 0), "wait", 0);

	sysgettimeofday(&t1, NULL);

	xchk(getrusage(RUSAGE_CHILDREN, &rv), "getrusage", NULL);

	tvdiff(&tv, &t1, &t0);
	report(&rv, &tv);

	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
