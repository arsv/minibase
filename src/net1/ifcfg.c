#include <bits/ioctl/socket.h>
#include <bits/socket/netlink.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <string.h>
#include <util.h>
#include <main.h>

#define IFF_UP (1<<0)

ERRTAG("ifcfg");

struct top {
	int argc;
	int argi;
	char** argv;
};

#define CTX struct top* ctx

static char* shift_arg(CTX)
{
	if(ctx->argi >= ctx->argc)
		fail("too few arguments", NULL, 0);

	return ctx->argv[ctx->argi++];
}

static void no_more_args(CTX)
{
	if(ctx->argi < ctx->argc)
		fail("too many arguments", NULL, 0);
}

static void get_flags(int fd, struct ifreq* ifr, char* name)
{
	int nlen = strlen(name);
	int ret;

	if(nlen > (int)sizeof(ifr->name))
		fail("device name too long", NULL, 0);

	memzero(ifr, sizeof(*ifr));
	memcpy(ifr->name, name, nlen);

	if((ret = sys_ioctl(fd, SIOCGIFFLAGS, ifr)) < 0)
		fail("ioctl SIOCGIFFLAGS", name, ret);
}

static void set_flags(int fd, struct ifreq* ifr, char* name)
{
	int ret;

	if((ret = sys_ioctl(fd, SIOCSIFFLAGS, ifr)) < 0)
		fail("ioctl SIOCSIFFLAGS", name, ret);
}

static int open_socket(void)
{
	int fd;

	if((fd = sys_socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE)) < 0)
		fail("socket", "PF_NETLINK", fd);

	return fd;
}

static void cmd_up(CTX, char* device)
{
	struct ifreq ifr;
	int fd = open_socket();

	no_more_args(ctx);

	get_flags(fd, &ifr, device);

	if(ifr.ival & IFF_UP)
		return;

	ifr.ival |= IFF_UP;

	set_flags(fd, &ifr, device);
}

static void cmd_down(CTX, char* device)
{
	struct ifreq ifr;
	int fd = open_socket();

	no_more_args(ctx);

	get_flags(fd, &ifr, device);

	if(!(ifr.ival & IFF_UP))
		return;

	ifr.ival &= ~IFF_UP;

	set_flags(fd, &ifr, device);
}

static void cmd_reset(CTX, char* device)
{
	struct ifreq ifr;
	int fd = open_socket();

	no_more_args(ctx);

	get_flags(fd, &ifr, device);

	if(!(ifr.ival & IFF_UP))
		goto up;

	ifr.ival &= ~IFF_UP;

	set_flags(fd, &ifr, device);
up:
	ifr.ival |= IFF_UP;

	set_flags(fd, &ifr, device);
}

static void prep_args(CTX, int argc, char** argv)
{
	memzero(ctx, sizeof(*ctx));
	ctx->argc = argc;
	ctx->argi = 1;
	ctx->argv = argv;
}

static const struct cmd {
	char name[8];
	void (*call)(CTX, char* device);
} commands[] = {
	{ "up",    cmd_up    },
	{ "down",  cmd_down  },
	{ "reset", cmd_reset }
};

static const struct cmd* find_command(char* name)
{
	const struct cmd* cc;

	for(cc = commands; cc < ARRAY_END(commands); cc++)
		if(!strncmp(cc->name, name, sizeof(cc->name)))
			return cc;

	fail("unknown command", name, 0);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	prep_args(ctx, argc, argv);

	char* device = shift_arg(ctx);
	char* command = shift_arg(ctx);

	const struct cmd* cc = find_command(command);

	cc->call(ctx, device);

	return 0;
}
