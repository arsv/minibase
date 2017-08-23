#include <sys/signal.h>

#include <string.h>
#include <format.h>
#include <fail.h>

ERRTAG = "kill";
ERRLIST = {
	REPORT(EINVAL), REPORT(EPERM), REPORT(ESRCH), REPORT(ENOSYS),
	RESTASNUMBERS
};

static const struct signame {
	int sig;
	char name[4];
} signames[] = {
#define SIG(a) { SIG##a, #a }
	SIG(HUP),
	SIG(INT),
	SIG(QUIT),
	SIG(ILL),
	SIG(ABRT),
	SIG(FPE),
	SIG(KILL),
	SIG(SEGV),
	SIG(PIPE),
	SIG(ALRM),
	SIG(TERM),
	SIG(USR1),
	SIG(USR2),
	SIG(CHLD),
	SIG(CONT),
	SIG(STOP),
	SIG(TSTP),
	SIG(TTIN),
	SIG(TTOU),
	{ 0, "" }
};

static int sigbyname(char* name)
{
	char* p;
	int sig;

	p = parseint(name, &sig);
	if(p && !*p) return sig;

	const struct signame* sn;
	for(sn = signames; sn->sig; sn++)
		if(!strncmp(sn->name, name, 4))
			return sn->sig;

	fail("unknown signal", name, 0);
}

static int killpid(int sig, char* pstr)
{
	int pid;
	int neg = 0;
	char* p = pstr;

	if(*p == '-') { neg = 1; p++; };

	if(!(p = parseint(p, &pid)) || *p)
		fail("not a number:", pstr, 0);

	long ret = sys_kill(neg ? -pid : pid, sig);

	if(ret < 0) {
		warn("cannot kill", pstr, ret);
		return -1;
	} else {
		return 0;
	}
}

int main(int argc, char** argv)
{
	int i = 1;
	int sig = SIGTERM;
	int ret = 0;

	if(i < argc && argv[i][0] == '-')
		sig = sigbyname(argv[i++] + 1);

	if(i >= argc)
		fail("need pids to kill", NULL, 0);

	for(; i < argc; i++)
		ret |= killpid(sig, argv[i]);

	return ret;
}
