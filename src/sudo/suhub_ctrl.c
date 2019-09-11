#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/timer.h>
#include <sys/file.h>

#include <nlusctl.h>
#include <cmsg.h>
#include <util.h>

#include "common.h"
#include "suhub.h"

char rxbuf[1024];
char txbuf[16];
char control[256]; /* ~18 bytes per cmsghdr, 4 to 5 cmsghdr-s */

int reply(int fd, int rep, int attr, int arg)
{
	struct ucbuf uc;

	uc_buf_set(&uc, txbuf, sizeof(txbuf));
	uc_put_hdr(&uc, rep);
	if(attr) uc_put_int(&uc, attr, arg);
	uc_put_end(&uc);

	return uc_send_timed(fd, &uc);
}

static void* get_scm(struct ucaux* ux, int type, int size)
{
	void* p = ux->buf;
	void* e = p + ux->len;
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

static int cmd_exec(int* cpid, struct ucmsg* msg, struct ucaux* ux)
{
	int nfds = 4;
	int* fds;
	struct ucred* cr;
	int argc;

	if(!(fds = get_scm(ux, SCM_RIGHTS, nfds*sizeof(int))))
		return -EINVAL;
	if(!(cr = get_scm(ux, SCM_CREDENTIALS, sizeof(*cr))))
		return -EINVAL;
	if(!(argc = count_args(msg)))
		return -EINVAL;

	char* argv[argc+1];
	prep_argv_array(argc, argv, msg);

	return spawn(cpid, argv, fds, cr);
}

static int cmd_kill(int* cpid, struct ucmsg* msg, struct ucaux* ux)
{
	(void)ux;
	int sig, *p;

	if((p = uc_get_int(msg, ATTR_SIGNAL)))
		sig = *p;
	else
		sig = SIGTERM;

	if(*cpid <= 0)
		return -ESRCH;

	return sys_kill(*cpid, sig);
}

static const struct cmd {
	int cmd;
	int (*call)(int* cpid, struct ucmsg* msg, struct ucaux* ux);
} cmds[] = {
	{ CMD_EXEC, cmd_exec },
	{ CMD_KILL, cmd_kill }
};

static void close_all_cmsg_fds(struct ucaux* ux)
{
	struct cmsg* cm;
	void* p = ux->buf;
	void* e = p + ux->len;

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

static int dispatch(int fd, int* cpid, struct ucmsg* msg, struct ucaux* ux)
{
	const struct cmd* p;

	for(p = cmds; p < ARRAY_END(cmds); p++)
		if(p->cmd == msg->cmd)
			return p->call(cpid, msg, ux);

	return -ENOSYS;
}

void handle(int fd, int* cpid)
{
	int ret;
	struct ucmsg* msg;
	struct ucaux ux = { control, sizeof(control) };

	if((ret = uc_recvmsg(fd, rxbuf, sizeof(rxbuf), &ux)) < 0)
		goto err;
	if(!(msg = uc_msg(rxbuf, ret)))
		goto err;

	ret = dispatch(fd, cpid, msg, &ux);

	if((ret = reply(fd, ret, 0, 0)) >= 0)
		goto out;
err:
	sys_shutdown(fd, SHUT_RDWR);
out:
	close_all_cmsg_fds(&ux);

}
