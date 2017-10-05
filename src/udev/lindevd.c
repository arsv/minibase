#include <bits/socket/netlink.h>
#include <bits/input/key.h>
#include <bits/input/abs.h>
#include <bits/input/rel.h>
#include <bits/major.h>
#include <bits/input.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/fpath.h>
#include <sys/creds.h>
#include <sys/dents.h>
#include <sys/socket.h>

#include <errtag.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "common.h"

ERRTAG("lindevd");

#define UDEV_MGRP_KERNEL   (1<<0)

struct uevent {
	char* ACTION;
	char* DEVPATH;
	char* SUBSYSTEM;
	char* MAJOR;
	char* MINOR;
	char* DEVNAME;
} uevent;

static int hasbit(char* bits, int size, int bit)
{
	if(bit >= 8*size)
		return 0;

	int byte = bits[bit/8];
	int mask = 1 << (bit % 8);

	return byte & mask;
}

static int getmask(int fd, int type, char* bits, int size)
{
	memzero(bits, size);

	return sys_ioctl(fd, EVIOCGBIT(type, size), bits);
}

static char* probe_keyboard(char* p, char* e, int dev)
{
	char keys[32];
	int size = sizeof(keys);

	if((getmask(dev, EV_KEY, keys, size)) < 0)
		return NULL;

	if(!hasbit(keys, size, KEY_ENTER))
		return NULL;
	if(!hasbit(keys, size, KEY_SPACE))
		return NULL;

	p = fmtstr(p, e, "E:ID_INPUT_KEY=1\n");
	p = fmtstr(p, e, "E:ID_INPUT_KEYBOARD=1\n");

	return p;
}

static char* probe_mouse(char* p, char* e, int dev)
{
	int size = 2;
	char abs[size];
	char rel[size];

	if((getmask(dev, EV_REL, rel, size)) < 0)
		return NULL;
	if((getmask(dev, EV_ABS, abs, size)) < 0)
		return NULL;

	char* q = p;

	if(hasbit(rel, size, REL_X) && hasbit(rel, size, REL_Y))
		p = fmtstr(p, e, "E:ID_INPUT_MOUSE=1\n");
	if(hasbit(abs, size, ABS_X) && hasbit(abs, size, ABS_Y))
		p = fmtstr(p, e, "E:ID_INPUT_TOUCHPAD=1\n");

	if(q == p)
		return NULL;

	return p;
}

static int open_dev(char* name)
{
	FMTBUF(p, e, path, 100);
	p = fmtstr(p, e, "/dev/input/");
	p = fmtstr(p, e, name);
	FMTEND(p, e);

	return sys_open(path, O_RDONLY);
}

static int open_env(char* maj, char* min, int fd)
{
	int ret;
	struct stat st;

	FMTBUF(p, e, path, 100);

	p = fmtstr(p, e, RUNUDEVDATA);
	p = fmtstr(p, e, "/c");

	if(maj && min) {
		p = fmtstr(p, e, maj);
		p = fmtstr(p, e, ":");
		p = fmtstr(p, e, min);
	} else if((ret = sys_fstat(fd, &st)) < 0) {
		return ret;
	} else {
		p = fmtlong(p, e, major(st.rdev));
		p = fmtstr(p, e, ":");
		p = fmtlong(p, e, minor(st.rdev));
	}

	FMTEND(p, e);	

	return sys_open3(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
}

static void handle(char* maj, char* min, char* name)
{
	int dev, env;
	char* q;

	if((dev = open_dev(name)) < 0)
		return;

	FMTBUF(p, e, data, 200);
	p = fmtstr(p, e, "E:ID_INPUT=1\n");

	if((q = probe_mouse(p, e, dev)))
		goto done;
	if((q = probe_keyboard(p, e, dev)))
		goto done;

	goto out;
done:
	if((env = open_env(maj, min, dev)) < 0)
		goto out;

	writeall(env, data, q - data);

	sys_close(env);
out:
	sys_close(dev);
}

static void remove(char* maj, char* min)
{
	FMTBUF(p, e, path, 100);

	p = fmtstr(p, e, RUNUDEVDATA);
	p = fmtstr(p, e, "/c");

	p = fmtstr(p, e, maj);
	p = fmtstr(p, e, ":");
	p = fmtstr(p, e, min);

	FMTEND(p, e);
	
	sys_unlink(path);
}

static void check_event(struct uevent* ue, int gone)
{
	if(!ue->SUBSYSTEM || !ue->DEVNAME)
		return;
	if(!ue->MAJOR || !ue->MINOR)
		return;
	if(strcmp(ue->SUBSYSTEM, "input"))
		return;
	if(strncmp(ue->DEVNAME, "input/", 6))
		return;

	char* name = ue->DEVNAME + 6;
	char* maj = ue->MAJOR;
	char* min = ue->MINOR;

	if(gone)
		remove(maj, min);
	else
		handle(maj, min, name);
}

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

#define fieldat(s, o) *((char**) ((void*)(s) + o) )

static void parse_event(struct uevent* ue, char* buf, int len)
{
	const struct field* f;
	char* end = buf + len;
	char* p = buf;
	char* q;

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

static void recv_udev_event(int fd)
{
	int rd, max = 1024;
	char buf[max+2];

	if((rd = sys_recv(fd, buf, max, 0)) < 0)
		fail("recv", "udev", rd);

	buf[rd] = '\0';

	int gone;

	if(!strncmp(buf, "remove@", 7))
		gone = 1;
	else if(!strncmp(buf, "add@", 4))
		gone = 0;
	else return;

	struct uevent ue;

	parse_event(&ue, buf, rd);

	check_event(&ue, gone);
}

static int open_udev(void)
{
	int fd, ret;

	int domain = PF_NETLINK;
	int type = SOCK_DGRAM;
	int proto = NETLINK_KOBJECT_UEVENT;

	if((fd = sys_socket(domain, type, proto)) < 0)
		fail("socket", "udev", fd);

	struct sockaddr_nl addr = {
		.family = AF_NETLINK,
		.pid = sys_getpid(),
		.groups = UDEV_MGRP_KERNEL
	};

	if((ret = sys_bind(fd, &addr, sizeof(addr))) < 0)
		fail("bind", "udev", ret);

	return fd;
}

static void scan_devices(void)
{
	char* dir = "/dev/input";
	int len = 1024;
	char buf[len];
	long fd, rd;

	if((fd = sys_open(dir, O_DIRECTORY)) < 0)
		fail("open", dir, fd);

	while((rd = sys_getdents(fd, buf, len)) > 0) {
		char* ptr = buf;
		char* end = buf + rd;
		while(ptr < end) {
			struct dirent* de = (struct dirent*) ptr;

			if(!de->reclen)
				break;

			ptr += de->reclen;

			if(dotddot(de->name))
				continue;
			if(de->type != DT_CHR)
				continue;
			if(strncmp(de->name, "event", 5))
				continue;

			handle(NULL, NULL, de->name);
		}
	} if(rd < 0) {
		fail("getdents", dir, rd);
	}

	sys_close(fd);
}

static void makedir(char* dir)
{
	int ret;

	if((ret = sys_mkdir(dir, 0755)) >= 0)
		return;
	if(ret == -EEXIST)
		return;

	fail(NULL, dir, ret);
}

int main(int argc, char** argv)
{
	(void)argv;

	if(argc > 1)
		fail("too many arguments", NULL, 0);

	int fd = open_udev();

	makedir(RUNUDEV);
	makedir(RUNUDEVDATA);

	scan_devices();

	while(1) recv_udev_event(fd);
}
