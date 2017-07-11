#include <bits/socket.h>
#include <bits/socket/unix.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/itimer.h>

#include <nlusctl.h>
#include <string.h>
#include <format.h>
#include <util.h>

#include "common.h"
#include "svmon.h"

#define REPLIED 1

#define CN struct conn* cn
#define MSG struct ucmsg* msg

static int send_reply(CN, struct ucbuf* uc)
{
	writeall(cn->fd, uc->brk, uc->ptr - uc->brk);
	return REPLIED;
}

static int reply(CN, int err)
{
	char cbuf[16];
	struct ucbuf uc;

	uc_buf_set(&uc, cbuf, sizeof(cbuf));
	uc_put_hdr(&uc, err);
	uc_put_end(&uc);

	return send_reply(cn, &uc);
}

static int estimate_list_size(void)
{
	int count = 0;
	struct proc* rc;

	for(rc = firstrec(); rc; rc = nextrec(rc))
		count++;

	return 10*count*sizeof(struct ucattr) + count*sizeof(*rc);
}

static void put_proc_entry(struct ucbuf* uc, struct proc* rc)
{
	struct ucattr* at;

	at = uc_put_nest(uc, ATTR_PROC);

	uc_put_int(uc, ATTR_PID, rc->pid);
	uc_put_str(uc, ATTR_NAME, rc->name);

	if(ring_buf_for(rc))
		uc_put_flag(uc, ATTR_RING);

	uc_end_nest(uc, at);
}

static int rep_list(CN)
{
	int size = estimate_list_size();
	char* buf = heap_alloc(size);

	if(!buf) return -ENOMEM;

	struct ucbuf uc = { buf, buf, buf + size, 0 };
	struct proc* rc;

	uc_put_hdr(&uc, 0);

	for(rc = firstrec(); rc; rc = nextrec(rc))
		put_proc_entry(&uc, rc);

	uc_put_end(&uc);

	return send_reply(cn, &uc);
}

static int reboot(char code)
{
	gg.rbcode = code;
	stop_all_procs();
	return 0;
}

static int cmd_reboot(CN, MSG)
{
	return reboot('r');
}

static int cmd_shutdown(CN, MSG)
{
	return reboot('h');
}

static int cmd_poweroff(CN, MSG)
{
	return reboot('p');
}

static int cmd_list(CN, MSG)
{
	return rep_list(cn);
}

static const struct cmd {
	int cmd;
	int (*call)(CN, MSG);
} commands[] = {
	{ CMD_LIST,     cmd_list     },
	{ CMD_REBOOT,   cmd_reboot   },
	{ CMD_SHUTDOWN, cmd_shutdown },
	{ CMD_POWEROFF, cmd_poweroff },
	{ 0,            NULL         }
};

static void dispatch(CN, MSG)
{
	const struct cmd* cd;
	int cmd = msg->cmd;
	int ret;

	for(cd = commands; cd->cmd; cd++)
		if(cd->cmd == cmd)
			break;
	if(!cd->cmd)
		reply(cn, -ENOSYS);
	else if((ret = cd->call(cn, msg)) <= 0)
		reply(cn, ret);

	heap_flush();
}

void close_conn(CN)
{
	sys_close(cn->fd);
	memzero(cn, sizeof(*cn));
	gg.pollset = 0;
}

void handle_conn(CN)
{
	int ret, fd = cn->fd;

	char rxbuf[500];
	struct urbuf ur = {
		.buf = rxbuf,
		.mptr = rxbuf,
		.rptr = rxbuf,
		.end = rxbuf + sizeof(rxbuf)
	};
	struct itimerval old, itv = {
		.interval = { 0, 0 },
		.value = { 1, 0 }
	};

	sys_setitimer(0, &itv, &old);

	while(1) {
		if((ret = uc_recv(fd, &ur, 0)) < 0)
			break;

		dispatch(cn, ur.msg);

		if(ur.mptr >= ur.rptr)
			break;
	}

	if(ret != -EBADF && ret != -EAGAIN)
		close_conn(cn);

	sys_setitimer(0, &old, NULL);
}

void accept_ctrl(int sfd)
{
	int cfd;
	struct sockaddr addr;
	int addr_len = sizeof(addr);
	struct conn *cn;

	while((cfd = sys_accept(sfd, &addr, &addr_len)) > 0)
		if((cn = grab_conn_slot()))
			cn->fd = cfd;
		else
			sys_close(cfd);

	gg.pollset = 0;
}

void setup_ctrl(void)
{
	int fd;
	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = SVCTL
	};
	const int flags = SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC;

	if((fd = sys_socket(AF_UNIX, flags, 0)) < 0)
		return report("socket", "AF_UNIX", fd);

	long ret;
	char* name = SVCTL;

	gg.ctrlfd = fd;
	gg.pollset = 0;

	if((ret = sys_bind(fd, &addr, sizeof(addr))) < 0)
		report("bind", name, ret);
	else if((ret = sys_listen(fd, 1)))
		report("listen", name, ret);
	else
		return;

	gg.ctrlfd = -1;
	sys_close(fd);
}
