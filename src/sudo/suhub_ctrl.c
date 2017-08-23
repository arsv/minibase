#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/sched.h>
#include <sys/file.h>

#include <nlusctl.h>
#include <cmsg.h>
#include <fail.h>
#include <util.h>

#include "common.h"
#include "suhub.h"

char rxbuf[1024];
char txbuf[16];
char control[256]; /* ~18 bytes per cmsghdr, 4 to 5 cmsghdr-s */

void reply(int fd, int rep, int attr, int arg)
{
	struct ucbuf uc = {
		.brk = txbuf,
		.ptr = txbuf,
		.end = txbuf + sizeof(txbuf)
	};

	if(fd < 0)
		return;

	uc_put_hdr(&uc, rep);
	if(attr) uc_put_int(&uc, attr, arg);
	uc_put_end(&uc);

	sys_send(fd, uc.brk, uc.ptr - uc.brk, 0);
}

void* get_scm(struct ucbuf* uc, int type, int size)
{
	void* p = uc->brk;
	void* e = uc->ptr;
	struct cmsg* cm;

	if(!(cm = cmsg_get(p, e, SOL_SOCKET, type)))
		return NULL;
	if(cmsg_paylen(cm) != size)
		return NULL;

	return cmsg_payload(cm);
}

static int count_args(struct ucmsg* msg)
{
	struct ucattr* at;
	int count = 0;

	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at))
		if(uc_is_str(at, ATTR_ARGV))
			count++;

	return count;
}

static void prep_argv_array(int argc, char** argv, struct ucmsg* msg)
{
	struct ucattr* at;
	char* arg;
	int i = 0;

	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at))
		if(!(arg = uc_is_str(at, ATTR_ARGV)))
			continue;
		else if(i >= argc)
			break; /* should not happen */
		else argv[i++] = arg;

	argv[i] = NULL;
}

static int cmd_exec(int* cpid, struct ucmsg* msg, struct ucbuf* uc)
{
	int nfds = 4;
	int* fds;
	struct ucred* cr;
	int argc;

	if(!(fds = get_scm(uc, SCM_RIGHTS, nfds*sizeof(int))))
		return -EINVAL;
	if(!(cr = get_scm(uc, SCM_CREDENTIALS, sizeof(*cr))))
		return -EINVAL;
	if(!(argc = count_args(msg)))
		return -EINVAL;

	char* argv[argc+1];
	prep_argv_array(argc, argv, msg);

	return spawn(cpid, argv, fds, cr);
}

static int cmd_kill(int* cpid, struct ucmsg* msg, struct ucbuf* uc)
{
	int sig, *p;

	if((p = uc_get_int(msg, ATTR_SIGNAL)))
		sig = *p;
	else
		sig = SIGTERM;

	if(*cpid <= 0)
		return -ESRCH;
	else
		return sys_kill(*cpid, sig);
}

static const struct cmd {
	int cmd;
	int (*call)(int* cpid, struct ucmsg* msg, struct ucbuf* uc);
} cmds[] = {
	{ CMD_EXEC, cmd_exec },
	{ CMD_KILL, cmd_kill }
};

static void close_all_cmsg_fds(struct ucbuf* uc)
{
	struct cmsg* cm;
	void* p = uc->brk;
	void* e = uc->ptr;

	for(cm = cmsg_first(p, e); cm; cm = cmsg_next(cm, e)) {
		if(cm->level != SOL_SOCKET)
			continue;
		if(cm->type != SCM_RIGHTS)
			continue;

		int* fp = cmsg_payload(cm);
		int* fe = cmsg_paylend(cm);

		for(; fp < fe; fp++)
			sys_close(*fp);
	}
}

static void dispatch_cmd(int fd, int* cpid, struct ucmsg* msg, struct ucbuf* uc)
{
	const struct cmd* p = cmds;
	const struct cmd* e = cmds + ARRAY_SIZE(cmds);
	int ret = -ENOSYS;

	for(; p < e; p++)
		if(p->cmd == msg->cmd) {
			ret = p->call(cpid, msg, uc);
			break;
		}

	reply(fd, ret, 0, 0);
	close_all_cmsg_fds(uc);
}

void handle(int fd, int* cpid)
{
	int ret;

	struct urbuf ur = {
		.buf = rxbuf,
		.mptr = rxbuf,
		.rptr = rxbuf,
		.end = rxbuf + sizeof(rxbuf)
	};
	struct ucbuf uc = {
		.brk = control,
		.ptr = control,
		.end = control + sizeof(control)
	};
	struct itimerval old, itv = {
		.interval = { 0, 0 },
		.value = { 1, 0 }
	};

	sys_setitimer(0, &itv, &old);

	while(1) {
		if((ret = uc_recvmsg(fd, &ur, &uc, 0)) < 0)
			break;
		dispatch_cmd(fd, cpid, ur.msg, &uc);
	}

	if(ret < 0 && ret != -EBADF && ret != -EAGAIN)
		sys_shutdown(fd, SHUT_RDWR);

	sys_setitimer(0, &old, NULL);
}
