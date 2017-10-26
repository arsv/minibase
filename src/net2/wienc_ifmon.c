#include <bits/socket/unix.h>
#include <sys/socket.h>
#include <sys/file.h>

#include <nlusctl.h>
#include <printf.h>
#include <util.h>

#include "common.h"
#include "wienc.h"

void trigger_dhcp(void)
{
	int fd, ret;
	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = IFCTL
	};

	if((fd = sys_socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		warn("socket", "AF_UNIX", fd);
		return;
	} if((ret = sys_connect(fd, &addr, sizeof(addr))) < 0) {
		warn("connect", addr.path, ret);
		goto out;
	}

	char buf[64];
	struct ucbuf uc = {
		.brk = buf,
		.ptr = buf,
		.end = buf + sizeof(buf)
	};

	uc_put_hdr(&uc, CMD_IF_XDHCP);
	uc_put_int(&uc, ATTR_IFI, ifindex);
	uc_put_end(&uc);

	int len = uc.ptr - uc.brk;

	if((ret = writeall(fd, buf, len)) < 0)
		warn("write", addr.path, ret);
out:
	sys_close(fd);
}
