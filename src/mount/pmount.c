#include <bits/socket/unix.h>

#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/creds.h>
#include <sys/file.h>

#include <errtag.h>
#include <nlusctl.h>
#include <sigset.h>
#include <cmsg.h>
#include <string.h>
#include <format.h>
#include <util.h>

#include "common.h"

ERRTAG("pmount");

#define OPTS "fu"
#define OPT_f (1<<0)
#define OPT_u (1<<1)

char txbuf[3072];
char rxbuf[32];
char ancillary[128];
int signal;

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

static void send_simple(int fd, char* buf, int len)
{
	int ret;

	if((ret = sys_send(fd, buf, len, 0)) < 0)
		fail("send", NULL, ret);
}

static int put_cmsg_fd(char* buf, int size, int fd)
{
	char* p = buf;
	char* e = buf + size;

	p = cmsg_put(p, e, SOL_SOCKET, SCM_RIGHTS, &fd, sizeof(fd));

	return p - buf;
}

static void send_with_anc(int fd, char* txbuf, int txlen, char* anc, int anlen)
{
	struct iovec iov = {
		.base = txbuf,
		.len = txlen
	};

	struct msghdr msg = {
		.iov = &iov,
		.iovlen = 1,
		.control = anc,
		.controllen = anlen
	};

	xchk(sys_sendmsg(fd, &msg, 0), "send", NULL);
}

static void recv_reply(int fd, int nlen)
{
	char rxbuf[80+nlen];
	struct ucmsg* msg;
	int rd;

	if((rd = sys_recv(fd, rxbuf, sizeof(rxbuf), 0)) < 0)
		fail("recv", NULL, rd);
	if(!(msg = uc_msg(rxbuf, rd)))
		fail("recv", NULL, -EBADMSG);
	if(msg->cmd == -ENODATA)
		fail("no valid filesystem found", NULL, 0);
	if(msg->cmd < 0)
		fail(NULL, NULL, msg->cmd);
	if(msg->cmd > 0)
		fail("unexpected reply", NULL, 0);

	char* mountpoint;

	if(!(mountpoint = uc_get_str(msg, ATTR_PATH)))
		return;

	int mplen = strlen(mountpoint);
	mountpoint[mplen] = '\n';

	sys_write(STDOUT, mountpoint, mplen + 1);
}

static int open_rw_or_ro(char* name)
{
	int fd;

	if((fd = sys_open(name, O_RDWR)) >= 0)
		return fd;
	if(fd != -EACCES)
		goto err;
	if((fd = sys_open(name, O_RDONLY)) >= 0)
		return fd;
err:
	fail(NULL, name, fd);
}

static void mount_file(char* name)
{
	int nlen = strlen(name);
	int ffd = open_rw_or_ro(name);
	int sfd = init_socket();

	char txbuf[20];
	struct ucbuf uc = {
		.brk = txbuf,
		.ptr = txbuf,
		.end = txbuf + sizeof(txbuf)
	};

	uc_put_hdr(&uc, CMD_MOUNT_FD);
	uc_put_end(&uc);

	int txlen = uc.ptr - uc.brk;

	char ancillary[32];
	int anlen = put_cmsg_fd(ancillary, sizeof(ancillary), ffd);

	send_with_anc(sfd, txbuf, txlen, ancillary, anlen);

	return recv_reply(sfd, nlen);
}

static void cmd_with_name(int cmd, char* name)
{
	int nlen = strlen(name);
	int sfd = init_socket();

	char txbuf[20 + nlen];
	struct ucbuf uc = {
		.brk = txbuf,
		.ptr = txbuf,
		.end = txbuf + sizeof(txbuf)
	};

	uc_put_hdr(&uc, cmd);
	uc_put_str(&uc, ATTR_NAME, name);
	uc_put_end(&uc);

	send_simple(sfd, uc.brk, uc.ptr - uc.brk);

	return recv_reply(sfd, nlen);
}

static void mount_dev(char* name)
{
	return cmd_with_name(CMD_MOUNT_DEV, name);
}

static void umount(char* name)
{
	return cmd_with_name(CMD_UMOUNT, name);
}

int main(int argc, char** argv)
{
	int i = 1, opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	if(i >= argc)
		fail("too few arguments", NULL, 0);
	if(i < argc - 1)
		fail("too many arguments", NULL, 0);

	char* name = argv[i];

	if(opts & OPT_u)
		umount(name);
	else if(opts & OPT_f)
		mount_file(name);
	else
		mount_dev(name);

	return 0;
}
