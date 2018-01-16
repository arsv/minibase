#include <bits/input/key.h>
#include <bits/input/rel.h>
#include <bits/input/abs.h>
#include <bits/major.h>

#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/dents.h>

#include <format.h>
#include <string.h>
#include <util.h>

#include "common.h"
#include "udevmod.h"

/* Libinput and applications based on libinput rely on files in /run/udev/data
   to provide input device classification (keyboard, pointer etc).
   In conventional systems that data is written by udevd with a bunch of rule
   files. This code below emlulates relevant functionality, allowing libudev
   applications to run on otherwise udev-free systems.

   Note this is an ugly hack that should not exist in a properly written
   system. There is no reason for libinput to rely on 3rd-party files to
   get the data it could just as well query itself from /sys.

   Current implementation does only the absolute minimum necessary to run
   unpatched weston with unpatched libinput. */

#define IN(k) in->k, ARRAY_SIZE(in->k)

static int hasbit(ulong* mask, int size, int bit)
{
	int word = 8*sizeof(ulong);
	int w = bit / word;
	int b = bit % word;

	if(w >= size)
		return 0;

	return !!(mask[w] & (1UL<<b));
}

static char* probe_ptr(char* p, char* e, struct evbits* in)
{
	if(hasbit(IN(rel), REL_X) && hasbit(IN(rel), REL_Y))
		p = fmtstr(p, e, "E:ID_INPUT_MOUSE=1\n");
	if(hasbit(IN(abs), ABS_X) && hasbit(IN(abs), ABS_Y))
		p = fmtstr(p, e, "E:ID_INPUT_TOUCHPAD=1\n");

	return p;
}

static char* probe_key(char* p, char* e, struct evbits* in)
{
	if(!hasbit(IN(key), KEY_ENTER))
		return p;
	if(!hasbit(IN(key), KEY_SPACE))
		return p;

	p = fmtstr(p, e, "E:ID_INPUT_KEY=1\n");
	p = fmtstr(p, e, "E:ID_INPUT_KEYBOARD=1\n");

	return p;
}

static void set_bits(ulong* mask, int n, char* str)
{
	char* p = str;
	ulong v;

	while(*p && (p = parsexlong(p, &v))) {
		for(int i = n - 1; i >= 1; i--)
			mask[i] = mask[i-1];
		mask[0] = v;

		while(*p == ' ') p++;
	}
}

static int prefixed(char* name, char* pref)
{
	int plen = strlen(pref);

	if(strncmp(name, pref, plen))
		return 0;

	return plen;
}

static void parse_bits(char* buf, int len, struct evbits* in)
{
	char* p = buf;
	char* q;
	char* e = p + len;
	int pl;

	memzero(in, sizeof(*in));

	while(p < e) {
		q = strecbrk(p, e, '\n'); *q = '\0';

		if((pl = prefixed(p, "KEY=")) > 0)
			set_bits(IN(key), p + pl);
		else if((pl = prefixed(p, "ABS=")) > 0)
			set_bits(IN(abs), p + pl);
		else if((pl = prefixed(p, "REL=")) > 0)
			set_bits(IN(rel), p + pl);
		else if((pl = prefixed(p, "LED=")) > 0)
			set_bits(IN(led), p + pl);
		else if((pl = prefixed(p, "SW=")) > 0)
			set_bits(IN(sw), p + pl);

		p = q + 1;
	}
}

static int load_event_bits(char* devname, struct evbits* in)
{
	char buf[1024];
	int fd, rd;

	FMTBUF(p, e, path, 100);
	p = fmtstr(p, e, "/sys/class/input/");
	p = fmtstr(p, e, devname);
	p = fmtstr(p, e, "/device/uevent");
	FMTEND(p, e);

	if((fd = sys_open(path, O_RDONLY)) < 0)
		return fd;

	memzero(in, sizeof(*in));

	if((rd = sys_read(fd, buf, sizeof(buf))) > 0)
		parse_bits(buf, rd, in);

	sys_close(fd);

	return 0;
}

static void write_udev_data(char* dataname, char* buf, int len)
{
	int fd;
	int flags = O_WRONLY | O_CREAT | O_TRUNC;
	int mode = 0644;

	if((fd = sys_open3(dataname, flags, mode)) < 0)
		return;

	writeall(fd, buf, len);

	sys_close(fd);
}

static void probe(char* dataname, char* devname)
{
	struct evbits bits;

	char data[200];
	char* p = data;
	char* e = data + sizeof(data);
	char* s;

	if(load_event_bits(devname, &bits) < 0)
		return;

	s = p = fmtstr(p, e, "E:ID_INPUT=1\n");

	p = probe_ptr(p, e, &bits);
	p = probe_key(p, e, &bits);

	if(p <= s) /* no useful information beyond ID_INPUT=1 */
		return;

	write_udev_data(dataname, data, p - data);
}

static int stat_dev_input(char* name, struct stat* st)
{
	FMTBUF(p, e, path, 50);
	p = fmtstr(p, e, "/dev/input/");
	p = fmtstr(p, e, name);
	FMTEND(p, e);

	return sys_stat(path, st);
}

static void probe_device(char* name)
{
	struct stat st;

	if(stat_dev_input(name, &st) < 0)
		return;

	FMTBUF(p, e, path, 100);

	p = fmtstr(p, e, RUNUDEVDATA);
	p = fmtstr(p, e, "/c");
	p = fmtuint(p, e, major(st.rdev));
	p = fmtstr(p, e, ":");
	p = fmtuint(p, e, minor(st.rdev));

	FMTEND(p, e);

	probe(path, name);
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

			probe_device(de->name);
		}
	} if(rd < 0) {
		warn("getdents", dir, rd);
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

void init_inputs(CTX)
{
	if(ctx->startup)
		return;

	makedir(RUNUDEV);
	makedir(RUNUDEVDATA);

	scan_devices();
}

/* These two are called for incoming messages with subsystem="input".
   There are two kinds of messages (and devices), inputN and eventM:

        add@/devices/platform/i8042/serio1/input/input13/event10
        ACTION=add
        DEVPATH=/devices/platform/i8042/serio1/input/input13/event10
        SUBSYSTEM=input
        MAJOR=13
        MINOR=74
        DEVNAME=input/event10

        add@/devices/platform/.../input/input10
        ACTION=add
        DEVPATH=/devices/platform/.../input/input10
        SUBSYSTEM=input
        NAME="Dell WMI hotkeys"
        PROP=0
        EV=13
        KEY=800000000000 0 0 1500b00000c00 4000000200300000 e000000000000 0
        MSC=10
        MODALIAS=input:...
 
   The data gets written for eventN but the bits needed to do so comes
   in inputM message. The code reacts to the eventN message and has to
   read the corresponding inputM uevent from /sys.

   Note N and M do not match in most cases! */

static int make_data_path(char* path, int size, struct mbuf* uevent)
{
	char* p = path;
	char* e = path + size - 1;

	char* maj = getval(uevent, "MAJOR");
	char* min = getval(uevent, "MINOR");

	if(!maj || !min) return -1; /* inputM or something else entirely */

	p = fmtstr(p, e, RUNUDEVDATA);
	p = fmtstr(p, e, "/c");
	p = fmtstr(p, e, maj);
	p = fmtstr(p, e, ":");
	p = fmtstr(p, e, min);

	*p = '\0';

	return 0;
}

void probe_input(CTX, struct mbuf* uevent)
{
	char path[100];
	char* name;

	if(!(name = getval(uevent, "DEVNAME")))
		return;
	if(make_data_path(path, sizeof(path), uevent) < 0)
		return;

	probe(path, basename(name));
}

void clear_input(CTX, struct mbuf* uevent)
{
	char path[100];

	if(make_data_path(path, sizeof(path), uevent) < 0)
		return;
	
	sys_unlink(path);
}
