#include <bits/socket/unix.h>

#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/creds.h>
#include <sys/file.h>

#include <nlusctl.h>
#include <sigset.h>
#include <cmsg.h>
#include <string.h>
#include <format.h>
#include <fail.h>
#include <util.h>
#include <exit.h>

#include "common.h"

ERRTAG = "sudo";

char txbuf[3072];
char rxbuf[32];
char ancillary[128];
int signal;

static void sighandler(int sig)
{
	signal = sig;
}

static void sigaction(int sig, struct sigaction* sa, char* tag)
{
	xchk(sys_sigaction(sig, sa, NULL), "sigaction", tag);
}

static void setup_signals(void)
{
	struct sigaction sa = {
		.handler = sighandler,
		.flags = SA_RESTORER,
		.restorer = sigreturn
	};

	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP,  &sa, NULL);
	sigaction(SIGALRM, &sa, NULL);
}

int init_socket(void)
{
	int fd, ret;
	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = CONTROL
	};

	if((fd = sys_socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		fail("socket", "AF_UNIX", fd);
	if((ret = sys_connect(fd, &addr, sizeof(addr))) < 0)
		fail("connect", addr.path, ret);

	return fd;
}

static int prep_message(char* cmd, int argn, char** args)
{
	struct ucbuf uc = {
		.brk = txbuf,
		.ptr = txbuf,
		.end = txbuf + sizeof(txbuf)
	};

	uc_put_hdr(&uc, CMD_EXEC);

	uc_put_str(&uc, ATTR_ARGV, cmd);

	for(int i = 0; i < argn; i++)
		uc_put_str(&uc, ATTR_ARGV, args[i]);

	uc_put_end(&uc);

	if(uc.over)
		fail("argument list too long", NULL, 0);

	return uc.ptr - uc.brk;
}

static int prep_ancillary(void)
{
	char* p = ancillary;
	char* e = ancillary + sizeof(ancillary);

	struct ucred cr = {
		.pid = sys_getpid(),
		.uid = sys_getuid(),
		.gid = sys_getgid()
	};

	int cwd = xchk(sys_open(".", O_DIRECTORY), "open", ".");
	int fds[4] = { 0, 1, 2, cwd };

	p = cmsg_put(p, e, SOL_SOCKET, SCM_CREDENTIALS, &cr, sizeof(cr));
	p = cmsg_put(p, e, SOL_SOCKET, SCM_RIGHTS, fds, sizeof(fds));

	if(!p) fail("out of ancillary space", NULL, 0);

	return p - ancillary;
}

static void start_command(int fd, char* cmd, int argn, char** args)
{
	int ret;
	
	int txlen = prep_message(cmd, argn, args);
	int anlen = prep_ancillary();

	struct iovec iov = {
		.base = txbuf,
		.len = txlen
	};

	struct msghdr msg = {
		.iov = &iov,
		.iovlen = 1,
		.control = &ancillary,
		.controllen = anlen
	};

	uc_dump((struct ucmsg*)txbuf);

	if((ret = sys_sendmsg(fd, &msg, MSG_NOSIGNAL)) < 0)
		fail("send", NULL, ret);
}

static int report_signal(char* command, int sig)
{
	FMTBUF(p, e, buf, 50);
	p = fmtstr(p, e, "killed by signal ");
	p = fmtint(p, e, sig);
	FMTEND(p);

	fail(command, buf, 0);
}

static int report_exit(int code)
{
	_exit(code);
}

static void report_dead(char* command, struct ucmsg* msg)
{
	if(!msg)
		fail("connection terminated", NULL, 0);
	if(msg->cmd < 0)
		fail(command, NULL, msg->cmd);

	int* ip;

	if((ip = uc_get_int(msg, ATTR_SIGNAL)))
		report_signal(command, *ip);
	else if((ip = uc_get_int(msg, ATTR_STATUS)))
		report_exit(*ip);
	else
		fail("child died of unknown cause", NULL, 0);
}

static void pass_signal(int fd)
{
	int ret;

	if(!signal)
		return;

	struct ucbuf uc = {
		.brk = txbuf,
		.ptr = txbuf,
		.end = txbuf + sizeof(txbuf)
	};

	uc_put_hdr(&uc, CMD_KILL);
	uc_put_int(&uc, ATTR_SIGNAL, signal);
	uc_put_end(&uc);

	int txlen = uc.ptr - uc.brk;

	if((ret = sys_send(fd, txbuf, txlen, MSG_NOSIGNAL)) < 0)
		fail("send", NULL, ret);
}

static void wait_listen(int fd, char* command)
{
	int ret;
	struct urbuf ur = {
		.buf = rxbuf,
		.mptr = rxbuf,
		.rptr = rxbuf,
		.end = rxbuf + sizeof(rxbuf)
	};

	int code;
	struct ucmsg* msg = NULL;
	int started = 0;

	while(1) {
		if((ret = uc_recv(fd, &ur, !0)) == -EINTR) {
			pass_signal(fd);
			continue;
		} else if(ret <= 0) {
			fail("recv", NULL, ret);
		}

		msg = ur.msg;
		code = msg->cmd;

		if(code == REP_DEAD) {
			break;
		} else if(code > 0) {
			continue;
		} else if(code < 0) {
			if(!started)
				break;
			warn(NULL, NULL, code);
		} else if(!started) {
			started = 1;
		}
	}

	report_dead(command, msg);
}

int main(int argc, char** argv)
{
	int i = 0;
	char* cmd = (char*)basename(argv[i++]);

	if(strcmp(errtag, cmd))
		; /* called via a named symlink */
	else if(argc < 2)
		fail("command name required", NULL, 0);
	else
		cmd = (char*)basename(argv[i++]);

	if(*cmd == '-')
		fail("no options allowed", NULL, 0);

	setup_signals();

	int fd = init_socket();

	start_command(fd, cmd, argc - i, argv + i);

	wait_listen(fd, cmd);

	return 0; /* never reached */
}
