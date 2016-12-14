#include <sys/socket.h>
#include <sys/bind.h>
#include <sys/recv.h>
#include <sys/getpid.h>
#include <sys/write.h>
#include <sys/alarm.h>
#include <sys/stat.h>
#include <sys/_exit.h>

#include <bits/netlink.h>

#include <string.h>
#include <format.h>
#include <util.h>
#include <fail.h>

ERRTAG = "waitdev";
ERRLIST = {
	REPORT(EBADF), REPORT(ECONNREFUSED), REPORT(EFAULT),
	REPORT(EINTR), REPORT(EINVAL), REPORT(ENOMEM), REPORT(ENOTCONN),
	REPORT(ENOTSOCK), REPORT(EAFNOSUPPORT), REPORT(ENOBUFS),
	REPORT(EPROTONOSUPPORT), RESTASNUMBERS
};

#define TIMEOUT 5
#define PAGE 4096

char uebuf[PAGE];

struct uevent {
	char* ACTION;
	char* DEVPATH;
	char* SUBSYSTEM;
	char* MAJOR;
	char* MINOR;
	char* DEVNAME;
};

struct field {
	char* name;
	int off;
} fields[] = {
#define F(a) { #a, offsetof(struct uevent, a) }
	F(ACTION),
	F(DEVPATH),
	F(SUBSYSTEM),
	F(MAJOR),
	F(MINOR),
	F(DEVNAME),
	{ NULL }
};

#define fieldat(s, o) *((char**)((void*)s + o))

void parse_uevent(struct uevent* ue, char* buf, int len)
{
	char* end = buf + len;
	char* p = buf;
	char* q;
	struct field* f;

	memset(ue, 0, sizeof(*ue));
	while(p < end) {
		if(!(q = strchr(p, '=')))
			goto next;	/* malformed line, wtf? */
		*q++ = '\0';

		for(f = fields; f->name; f++)
			if(!strcmp(f->name, p))
				break;
		if(f->name)
			fieldat(ue, f->off) = q;

		next: p += strlen(p) + 1;
	}
}

int prep_netlink(void)
{
	struct sockaddr_nl nls;

	memset(&nls, 0, sizeof(struct sockaddr_nl));

	nls.nl_family = AF_NETLINK;
	nls.nl_pid = sysgetpid();
	nls.nl_groups = -1;

	int domain = PF_NETLINK;
	int type = SOCK_DGRAM;
	int protocol = NETLINK_KOBJECT_UEVENT;

	int fd = xchk(syssocket(domain, type, protocol), "socket", NULL);

	xchk(sysbind(fd, (void*)&nls, sizeof(nls)), "bind", NULL);

	return fd;
}

static int check_added_dev(int ndev, char** names, int* check, char* dev)
{
	int i;

	if(!dev)
		return 0;

	for(i = 0; i < ndev; i++) {
		if(check[i])
			continue;
		if(strcmp(dev, names[i]))
			continue;

		check[i] = 1;
		return 1;
	}

	return 0;
}

static void wait_for_uevents(int argc, char** argv, int* check, int missing)
{
	int fd = prep_netlink();
	long rd;
	struct uevent ue;

	while((rd = sysrecv(fd, uebuf, sizeof(uebuf)-1, 0)) > 0) {
		uebuf[rd] = '\0';
		if(!strcmp(uebuf, "libudev"))
			continue;

		parse_uevent(&ue, uebuf, rd);

		if(!ue.ACTION)
			continue;
		if(strcmp(ue.ACTION, "add"))
			continue;

		if(check_added_dev(argc, argv, check, ue.DEVNAME))
			missing--;

		if(!missing)
			break;
	} if(rd < 0)
		fail("recv", NULL, rd);
}

static int check_dev(const char* name)
{
	static const char pref[] = "/dev/";
	int namelen = strlen(name);
	int preflen = strlen(pref);
	int pathlen = preflen + namelen + 1;

	char path[pathlen];
	char* p = path;
	memcpy(p, pref, preflen); p += preflen;
	memcpy(p, name, namelen); p += namelen;
	*p++ = '\0';

	struct stat st;

	if(sysstat(path, &st) < 0)
		return 0;

	int type = st.st_mode & S_IFMT;

	if(type == S_IFBLK || type == S_IFCHR)
		return 1;

	fail("not a device:", path, 0);
}

static int try_stat_files(int ndev, char** names, int* check)
{
	int i;
	int missing = 0;

	for(i = 0; i < ndev; i++) {
		if(check_dev(names[i]))
			check[i] = 1;
		else
			missing++;
	}

	return missing;
}

static int intval(char* arg)
{
	int n;
	char* p = parseint(arg, &n);

	if(!p || *p)
		fail("not a number:", arg, 0);

	return n;
}

int main(int argc, char** argv)
{
	int i = 1;
	int timeout;

	if(i < argc && argv[i][0] == '+')
		timeout = intval(argv[i++] + 1);
	else
		timeout = TIMEOUT;

	if(i >= argc)
		fail("too few arguments", NULL, 0);

	argc -= i;
	argv += i;

	int check[argc];
	memset(check, 0, argc*sizeof(int));

	int missing = try_stat_files(argc, argv, check);

	if(!missing)
		return 0;

	sysalarm(timeout);
	wait_for_uevents(argc, argv, check, missing);

	return 0;
}
