#include <bits/socket.h>
#include <bits/socket/unix.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/kill.h>
#include <sys/itimer.h>

#include <nlusctl.h>
#include <string.h>
#include <format.h>
#include <alloca.h>
#include <util.h>

#include "common.h"
#include "vtmux.h"

#define NOERROR 0
#define REPLIED 1

#define CN struct conn* cn
#define MSG struct ucmsg* msg

static char txbuf[100]; /* for small replies */
static struct ucbuf uc;

static void start_reply(int cmd, int expected)
{
	char* buf = NULL;
	int len;

	buf = txbuf;
	len = sizeof(txbuf);

	uc.brk = buf;
	uc.ptr = buf;
	uc.end = buf + len;

	uc_put_hdr(&uc, cmd);
}

static int send_reply(CN)
{
	uc_put_end(&uc);

	writeall(cn->fd, uc.brk, uc.ptr - uc.brk);

	return REPLIED;
}

static int reply(CN, int err)
{
	start_reply(err, 0);

	return send_reply(cn);
}

static int cmd_switch(CN, MSG)
{
	int ret, *tty;

	if(!(tty = uc_get_int(msg, ATTR_TTY)))
		return -EINVAL;

	if((ret = switchto(*tty)) < 0)
		return ret;

	return reply(cn, 0);
}

/* No up-directory escapes here. Only basenames */

static int name_is_simple(const char* name)
{
	const char* p;

	if(!*name)
		return 0;
	for(p = name; *p; p++)
		if(*p == '/')
			return 0;

	return 1;
}

static int cmd_spawn(CN, MSG)
{
	char* name;
	int tty;

	if(!(name = uc_get_str(msg, ATTR_NAME)))
		return -EINVAL;
	if(!name_is_simple(name))
		return -EACCES;
	if((tty = query_empty_tty()) <= 0)
		return -ENOTTY;

	int ret = spawn(tty, name);

	return reply(cn, ret);
}

static const struct cmd {
	int cmd;
	int (*call)(CN, MSG);
} commands[] = {
	{ CMD_SWITCH,  cmd_switch },
	{ CMD_SPAWN,   cmd_spawn  },
	{ 0,           NULL       }
};

int dispatch_cmd(CN, MSG)
{
	const struct cmd* cd;
	int cmd = msg->cmd;
	int ret;

	for(cd = commands; cd->cmd; cd++)
		if(cd->cmd == cmd)
			break;
	if(!cd->cmd)
		ret = reply(cn, -ENOSYS);
	else if((ret = cd->call(cn, msg)) <= 0)
		ret = reply(cn, ret);

	return ret;
}
