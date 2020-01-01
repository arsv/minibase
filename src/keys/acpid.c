#include <bits/errno.h>
#include <sys/proc.h>
#include <sys/creds.h>
#include <sys/socket.h>

#include <netlink.h>
#include <netlink/pack.h>
#include <netlink/attr.h>
#include <netlink/recv.h>
#include <netlink/genl/ctrl.h>

#include <format.h>
#include <string.h>
#include <dirs.h>
#include <util.h>
#include <main.h>

#include "common.h"

ERRTAG("acpid");

struct acpievent {
        char cls[20];
        char bus[15];
        int type;
        int data;
};

#define CTX struct top* ctx

static const struct action {
	char cls[20];
	int data;
	char script[20];
} actions[] = {
	{ "ac_adapter",   0, "battery"  },
	{ "ac_adapter",   1, "acpower"  },
	{ "button/sleep", 1, "sleepbtn" },
	{ "button/power", 1, "powerbtn" }
};

static void spawn_handler(const char* script, char** envp)
{
	int pid, status, ret;

	FMTBUF(p, e, path, 200);
	p = fmtstr(p, e, CONFDIR "/");
	p = fmtstr(p, e, script);
	FMTEND(p, e);

	if((pid = sys_fork()) < 0) {
		warn("fork", NULL, pid);
		return;
	} else if(pid == 0) {
		char* argv[] = { path, NULL };
		ret = sys_execve(*argv, argv, envp);
		if(ret != -ENOENT && ret != -ENOTDIR)
			fail("exec", path, ret);
		_exit(ret ? -1 : 0);
	}

	/* TODO: timeout here? */
	if((ret = sys_waitpid(pid, &status, 0)) < 0)
		warn("waitpid", path, ret);
}

static const struct action* event_action(struct acpievent* evt)
{
	const struct action* act = actions;
	const struct action* end = ARRAY_END(actions);
	int cmplen = sizeof(evt->cls);

	for(; act < end; act++) {
		if(strncmp(act->cls, evt->cls, cmplen))
			continue;
		if(act->data != evt->data)
			continue;

		return act;
	}

	return NULL;
}

static struct acpievent* get_acpi_event(struct nlmsg* nlm)
{
	struct nlgen* msg;
	struct acpievent* evt;

	if(!(msg = nl_gen(nlm)))
		return NULL;
	if(msg->cmd != 1)
		return NULL;

	return nl_get_of_len(msg, 1, sizeof(*evt));
}

static int fetch_group_id(struct nlattr* groups, char* name)
{
	struct nlattr* at;

	for(at = nl_sub_0(groups); at; at = nl_sub_n(groups, at)) {
		if(!nl_attr_is_nest(at))
			continue;

		char* gn = nl_sub_str(at, 1);
		int* id = nl_sub_i32(at, 2);

		if(!gn || !id)
			continue;
		if(!strcmp(gn, name))
			return *id;
	}

	return -ENOENT;
}

static void subscribe_group(int fd, void* rxbuf, int rxlen)
{
	struct ncbuf nc;
	byte txbuf[64];
	struct nlmsg* msg;
	struct nlgen* gen;
	struct nlerr* err;
	char* family = "acpi_event";
	char* group = "acpi_mc_group";
	int ret, gid;

	nc_buf_set(&nc, txbuf, sizeof(txbuf));

	nc_header(&nc, GENL_ID_CTRL, 0, 0);
	nc_gencmd(&nc, CTRL_CMD_GETFAMILY, 1);
	nc_put_str(&nc, CTRL_ATTR_FAMILY_NAME, family);

	if((ret = nc_send(fd, &nc)) < 0)
		fail("send", "NETLINK", ret);
	if((ret = nl_recv(fd, rxbuf, rxlen)) < 0)
		fail("recv", "NETLINK", ret);
	if(!(msg = nl_msg(rxbuf, ret)) || (ret != nl_len(msg)))
		fail("recv", "NETLINK", -EBADMSG);
	if((err = nl_err(msg)))
		fail("netlink", "GETFAMILY", err->errno);
	else if(!(gen = nl_gen(msg)))
		fail("netlink", "GETFAMILY", -EBADMSG);

	struct nlattr* groups;

	if(!(groups = nl_get_nest(gen, CTRL_ATTR_MCAST_GROUPS)))
		fail("missing NL groups in", family, 0);
	if((gid = fetch_group_id(groups, group)) < 0)
		fail("missing NL group", group, 0);
	if((ret = nl_subscribe(fd, gid)) < 0)
		fail("subscribe", group, ret);
}

static int open_netlink_socket(void)
{
	int domain = PF_NETLINK;
	int protocol = NETLINK_GENERIC;
	int type = SOCK_RAW;
	struct sockaddr_nl nls = {
		.family = AF_NETLINK,
		.pid = sys_getpid(),
		.groups = 0
	};
	int fd, ret;

	if((fd = sys_socket(domain, type, protocol)) < 0)
		fail("socket", "NETLINK", fd);
	if((ret = sys_bind(fd, (struct sockaddr*)&nls, sizeof(nls))) < 0)
		fail("bind", "NETLINK", ret);

	return fd;
}

int main(int argc, char** argv)
{
	char** envp = argv + argc + 1;
	struct nlmsg* msg;
	struct acpievent* evt;
	const struct action* act;
	struct nrbuf nr;
	char buf[2048];
	int ret, fd;

	if(argc > 1)
		fail("too many arguments", NULL, 0);

	fd = open_netlink_socket();
	subscribe_group(fd, buf, sizeof(buf));
	nr_buf_set(&nr, buf, sizeof(buf));
recv:
	if((ret = nr_recv(fd, &nr)) < 0)
		fail("recv", "NETLINK", ret);
next:
	if(!(msg = nr_next(&nr)))
		goto recv;
	if(!(evt = get_acpi_event(msg)))
		goto next;
	if(!(act = event_action(evt)))
		goto next;

	spawn_handler(act->script, envp);

	goto next;
}
