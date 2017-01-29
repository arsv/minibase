#include <bits/socket.h>
#include <bits/socket/unix.h>
#include <sys/accept.h>
#include <sys/alarm.h>
#include <sys/bind.h>
#include <sys/close.h>
#include <sys/getsockopt.h>
#include <sys/getuid.h>
#include <sys/kill.h>
#include <sys/listen.h>
#include <sys/read.h>
#include <sys/socket.h>
#include <sys/write.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "svmon.h"

#define PONLY 0
#define GROUP 1

static void killrec(struct svcrec* rc, int group, int sig)
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
	struct svcrec* rc;

	for(rc = firstrec(); rc; rc = nextrec(rc)) {
		struct ringbuf* rg = ringfor(rc);

		p = fmtstr(p, e, " ");

		if(rc->pid > 0)
			p = fmtpad(p, e, 5, fmtint(p, e, rc->pid));
		else
			p = fmtpad(p, e, 5, fmtstr(p, e, "-"));

		p = fmtstr(p, e, rg ? "*" : " ");

		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, rc->name);
		p = fmtstr(p, e, "\n");
	}

	writeall(gg.outfd, buf, p - buf);

	afree();
}

static void showring(struct svcrec* rc)
{
	struct ringbuf* rg = ringfor(rc);

	if(!rg) return;

	char* buf = rg->buf;
	int ptr = rg->ptr;
	int off = ptr % RINGSIZE;

	writeall(gg.outfd, "#", 1);

	if(ptr >= RINGSIZE)
		writeall(gg.outfd, buf + off, RINGSIZE - off);

	writeall(gg.outfd, buf, off);
}

static void dumpidof(struct svcrec* rc)
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

static void disable(struct svcrec* rc)
{
	rc->lastsig = 0;
	rc->flags |= P_DISABLED;
	gg.state |= S_PASSREQ;
}

static void enable(struct svcrec* rc)
{
	rc->lastrun = 0;
	rc->flags &= ~P_DISABLED;
	gg.state |= S_PASSREQ;
}

static void reboot(char code)
{
	gg.rbcode = code;
	stopall();
}

static void restart(struct svcrec* rc)
{
	if(rc->pid > 0)
		syskill(rc->pid, SIGTERM);

	if(rc->flags & P_DISABLED) {
		rc->flags &= ~P_DISABLED;
		gg.state |= S_PASSREQ;
	}

	flushring(rc);
}

static void flusharg(struct svcrec* rc)
{
	if(rc)
		flushring(rc);
	else for(rc = firstrec(); rc; rc = nextrec(rc))
		flushring(rc);
}

static void parsecmd(char* cmd)
{
	char* arg = cmd + 1;
	struct svcrec* rc = NULL;

	/* Check whether this command needs arguments */
	switch(*cmd) {
		/* Optional arg */
		case 'f':
			if(!*arg) break;

		/* Mandatory argument */
		case 'x':             /* restart */
		case 'd': case 'e':   /* stop-start (disable-enable) */
		case 's': case 'c':   /* pause-resume (stop-continue) */
		case 'u': case 'i':   /* hup, pidof */
		case 'q':             /* show */
			if(!(rc = findrec(arg)))
				return report("no entry named", arg, 0);
			break;

		/* Anything else is no-argument */
		default: if(*arg)
			return report("no argument allowed for", cmd, 0);
	}

	/* Now the command itself */
	switch(*cmd) {
		/* halt */
		case 'h': reboot('h'); break;
		case 'p': reboot('p'); break;
		case 'r': reboot('r'); break;
		/* process ops */
		case 'x': restart(rc); break;
		case 'd': disable(rc); break;
		case 'e': enable(rc); break;
		case 's': killrec(rc, GROUP, SIGSTOP); break;
		case 'c': killrec(rc, GROUP, SIGCONT); break;
		case 'u': killrec(rc, PONLY, SIGHUP); break;
		/* state query */
		case 'l': dumpstate(); break;
		case 'i': dumpidof(rc); break;
		case 'q': showring(rc); break;
		case 'f': flusharg(rc); break;
		/* reconfigure */
		case 'z': reload(); break;
		default: report("unknown command", cmd, 0);
	}
}

static int checkuser(int fd)
{
	struct ucred cred;
	int credlen = sizeof(cred);

	if(sysgetsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &credlen))
		return -1;

	if(cred.uid != gg.uid)
		return -1;

	return 0;
}

static void readcmd(int fd)
{
	int rb;
	char cbuf[NAMELEN+10];

	if((rb = sysread(fd, cbuf, NAMELEN+1)) < 0)
		return report("recvmsg", NULL, rb);
	if(rb >= NAMELEN)
		return report("recvmsg", "message too long", 0);
	cbuf[rb] = '\0';

	gg.outfd = fd;
	parsecmd(cbuf);
	gg.outfd = STDERR;
}

void setctl(void)
{
	int fd;
	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = SVCTL
	};

	/* This way readable "@initctl" can be used for reporting below,
	   and config.h looks better too. */
	if(addr.path[0] == '@')
		addr.path[0] = '\0';

	/* we're not going to block for connections, just accept whatever
	   is already there; so it's SOCK_NONBLOCK */
	const int flags = SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC;
	if((fd = syssocket(AF_UNIX, flags, 0)) < 0)
		return report("socket", "AF_UNIX", fd);

	long ret;
	char* name = SVCTL;

	setctrlfd(fd);

	if((ret = sysbind(fd, &addr, sizeof(addr))) < 0)
		report("bind", name, ret);
	else if((ret = syslisten(fd, 1)))
		report("listen", name, ret);
	else
		return;

	setctrlfd(-1);
}

void acceptctl(int sfd)
{
	int cfd;
	int gotcmd = 0;
	struct sockaddr addr;
	int addr_len = sizeof(addr);

	while((cfd = sysaccept(sfd, &addr, &addr_len)) > 0) {
		int nonroot = checkuser(cfd);

		if(nonroot) {
			const char* denied = "Access denied\n";
			syswrite(cfd, denied, strlen(denied));
		} else {
			gotcmd = 1;
			sysalarm(SVCTL_TIMEOUT);
			readcmd(cfd);
		}

		sysclose(cfd);

	} if(gotcmd) {
		/* disable the timer in case it has been set */
		sysalarm(0);
	}
}
