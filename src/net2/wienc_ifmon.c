#include <bits/socket/unix.h>
#include <sys/socket.h>
#include <sys/file.h>

#include <nlusctl.h>
#include <printf.h>
#include <util.h>

#include "common.h"
#include "wienc.h"

/* For regular wired links, dhcp gets run once the link reports carrier
   acquisition (IFF_RUNNING). This does not work with 802.11: carrier
   means the link is associated, but regular packets are not allowed
   through until EAPOL exchange is completed. Running dhcp concurrently
   with EAPOL means the first DHCPREQUEST packet often gets lost,
   which in turn means unnecessary resend timeout.

   There's no way for ifmon to detect the end of EAPOL exchnage on its
   own, in part because key installation does not seem to generate any
   notifications whatsoever, and in part because ifmon has no idea whether
   the keys will be installed at all (we may be running unencrypted link).

   So the workaround here is to suppress normal dhcp logic for wifi links,
   and let EAPOL code notify ifmon when it's ok to start dhcp.

   Socket communication is done directly instead of spawning `ifctl`,
   it's simple enough to allow this.

   Current implementation does not handle possible dhcp failures. */

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
