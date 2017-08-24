#include <bits/socket/unix.h>
#include <sys/socket.h>

#include <format.h>
#include <string.h>
#include <fail.h>

#include "common.h"

ERRTAG = "logger";

static int parse_mode(char* mode)
{
	char* modes = "xacewnid";
	char* p;

	if(!mode[0] || mode[1])
		fail("invalid options", NULL, 0);

	for(p = modes; *p; p++)
		if(*p == mode[0])
			break;
	if(!*p)
		fail("invalid severity", mode, 0);

	return p - mode;
}

static int isspace(int c)
{
	return (c == ' ' || c == '\t' || c == '\n');
}

static int prefixed(char* str)
{
	char* p;

	for(p = str; *p; p++)
		if(isspace(*p))
			return 0;
		else if(*p == ':')
			break;

	if(*p != ':')
		return 0;
	if(!isspace(*(p+1)))
		return 0;

	return 1;
}

static void send_message(int prio, char* msg, int ap)
{
	int fd, ret;
	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = DEVLOG
	};

	FMTBUF(p, e, buf, strlen(msg) + 30);

	p = fmtstr(p, e, "<");
	p = fmtint(p, e, prio);
	p = fmtstr(p, e, "> ");
	if(ap) p = fmtstr(p, e, "logger: ");
	p = fmtstr(p, e, msg);

	FMTEND(p, e);

	if((fd = sys_socket(AF_UNIX, SOCK_DGRAM, 0)) < 0)
		fail("socket", NULL, fd);
	if((ret = sys_sendto(fd, buf, p - buf, 0, &addr, sizeof(addr))) < 0)
		fail("send", NULL, ret);
}

int main(int argc, char** argv)
{
	int i = 1;
	int prio = (1<<8); /* user-level message */

	if(i < argc && argv[i][0] == '-')
		prio |= parse_mode(argv[i++] + 1);
	else
		prio |= 6; /* informational */

	if(i >= argc)
		fail("too few arguments", NULL, 0);
	if(i < argc - 1)
		fail("too many arguments", NULL, 0);

	char* msg = argv[i];
	int pr = prefixed(msg);

	send_message(prio, msg, !pr);

	return 0;
}
