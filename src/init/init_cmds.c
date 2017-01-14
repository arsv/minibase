#include <sys/kill.h>
#include <sys/write.h>

#include <format.h>
#include <util.h>

#include "init.h"

#define PONLY 0
#define GROUP 1

static void killrec(struct initrec* rc, int group, int sig)
{
	int pid = rc->pid;

	if(group) pid = -pid;

	syskill(pid, sig);
}

static void dumpstate(void)
{
	int blen = PAGE;
	char* buf = alloc(blen);

	if(!buf) return;

	char* p = buf;
	char* e = buf + blen;
	struct initrec* rc;

	for(rc = firstrec(); rc; rc = nextrec(rc)) {
		if(rc->pid > 0)
			p = fmtpad(p, e, 5, fmtint(p, e, rc->pid));
		else
			p = fmtpad(p, e, 5, fmtstr(p, e, "-"));
		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, rc->name);
		p = fmtstr(p, e, "\n");
	}

	writeall(gg.outfd, buf, p - buf);
}

static void dumpidof(struct initrec* rc)
{
	char buf[40];

	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	if(rc->pid <= 0)
		return;

	p = fmtstr(p, e, "#");
	p = fmtint(p, e, rc->pid);
	*p++ = '\n';

	syswrite(gg.outfd, buf, p - buf);
}

static void disable(struct initrec* rc)
{
	rc->lastsig = 0;
	rc->flags |= P_DISABLED;
}

static void enable(struct initrec* rc)
{
	rc->lastrun = 0;
	rc->flags &= ~P_DISABLED;
}

void parsecmd(char* cmd)
{
	char* arg = cmd + 1;
	struct initrec* rc = NULL;

	/* Check whether this command needs arguments */
	switch(*cmd) {
		/* Mandatory argument */
		case 'r':		/* restart */
		case 'e': case 'd':	/* enable, disable */
		case 'p': case 'w':	/* pause, resume */
		case 'h': case 'i':	/* hup, pidof */
			if(!(rc = findrec(arg)))
				return report("no entry named", arg, 0);
			break;

		/* There are no commands with optional arguments atm */
		/* There are few that take no argument at all however */
		default: if(*arg)
			return report("no argument allowed for", cmd, 0);
	}

	/* Now the command itself */
	switch(*cmd) {
		/* halt */
		case 'H': gg.rbcode = 'h'; break;
		case 'P': gg.rbcode = 'p'; break;
		case 'R': gg.rbcode = 'r'; break;
		/* process ops */
		case 'p': killrec(rc, GROUP, SIGSTOP); break;
		case 'w': killrec(rc, GROUP, SIGCONT); break;
		case 'h': killrec(rc, PONLY, SIGHUP); break;
		case 'd': disable(rc); break;
		case 'e': enable(rc); break;
		/* state query */
		case 'i': dumpidof(rc); break;
		case '?': dumpstate(); break;
		/* reconfigure */
		case 'c': reload(); break;
		default: report("unknown command", cmd, 0);
	}
}
