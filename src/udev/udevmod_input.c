#include <bits/input/key.h>
#include <bits/input/rel.h>
#include <bits/input/abs.h>
#include <bits/major.h>

#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/dents.h>
#include <sys/sync.h>

#include <config.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "udevmod.h"

/* Most apps written with libudev need their input devices tagged
   with ID_INPUT*. Non-marked devices will be ignored.

   The tags must be added to the events before re-broadcasting them,
   but also stored under /run/udev/data for libudev to read while
   pre-scanning devices. Note libudev will *not* read those files
   during normal event processing, the events must carry all the data
   with them.

   There's clearly some confusion between eventN and inputM. It looks
   like inputM are the primary devices however not reporting eventN
   (with corresponding ID_INPUT_* tags set!) breaks things. For the lack
   of better options, the code below adds the tags both for inputM and
   eventN devices.

   In systemd-udevd, this tagging happens via ENV{...} clauses and it's
   not really input-specific. In minibase however there are no other
   uses so it's limited to input devices for now.

   The whole idea is stupid and inherently racy. The code here is just
   a stopgap to allow running unpatched clients until better fixes are
   implemented. The right way to fix this is to remove libudev/libinput
   dependencies and make the clients pick event bits directly from /sys. */

#define IN(k) in->k, ARRAY_SIZE(in->k)

static void append(CTX, char* str)
{
	int len = strlen(str);

	int size = sizeof(ctx->uevent) - 2;
	int left = size - ctx->ptr;

	if(len + 1 >= left)
		return warn("no space for", str, 0);

	char* ptr = ctx->uevent + ctx->ptr;

	memcpy(ptr, str, len);
	ptr[len] = '\0';

	ctx->ptr += len + 1;
}

static int hasbit(ulong* mask, int size, int bit)
{
	int word = 8*sizeof(ulong);
	int w = bit / word;
	int b = bit % word;

	if(w >= size)
		return 0;

	return !!(mask[w] & (1UL<<b));
}

static void probe_ptr(CTX, struct evbits* in)
{
	if(hasbit(IN(rel), REL_X) && hasbit(IN(rel), REL_Y))
		append(ctx, "ID_INPUT_MOUSE=1");
	if(hasbit(IN(abs), ABS_X) && hasbit(IN(abs), ABS_Y))
		append(ctx, "ID_INPUT_TOUCHPAD=1");
}

static void probe_key(CTX, struct evbits* in)
{
	if(nonzero(IN(key)))
		append(ctx, "ID_INPUT_KEY=1");

	if(!hasbit(IN(key), KEY_ENTER))
		return;
	if(!hasbit(IN(key), KEY_SPACE))
		return;

	append(ctx, "ID_INPUT_KEYBOARD=1");
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

static void parse_bits(char* buf, int len, struct evbits* in, char eol)
{
	char* p = buf;
	char* q;
	char* e = p + len;
	int pl;

	memzero(in, sizeof(*in));

	while(p < e) {
		q = strecbrk(p, e, eol);

		if(eol) *q = '\0';

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

		if(eol) *q = eol;

		p = q + 1;
	}
}

/* For eventN devices, load the bits from corresponding inputM
   entry in /sys (accessible via /sys/class/input/eventN/device).

   For inputM devices, the bits are already in the event body
   so there's no point in reading anything from /sys. */

static int load_event_bits(char* name, struct evbits* in)
{
	char buf[1024];
	int fd, rd;

	FMTBUF(p, e, path, 100);
	p = fmtstr(p, e, "/sys/class/input/");
	p = fmtstr(p, e, name);
	p = fmtstr(p, e, "/device/uevent");
	FMTEND(p, e);

	if((fd = sys_open(path, O_RDONLY)) < 0)
		return fd;

	memzero(in, sizeof(*in));

	if((rd = sys_read(fd, buf, sizeof(buf))) > 0)
		parse_bits(buf, rd, in, '\n');

	sys_fsync(fd);
	sys_close(fd);

	return 0;
}

static void read_event_bits(CTX, struct evbits* in)
{
	char* buf = ctx->uevent;
	int len = ctx->sep;

	parse_bits(buf, len, in, '\0');
}

static void probe(CTX, char* name)
{
	struct evbits bits;

	if(!strncmp(name, "input", 5))
		read_event_bits(ctx, &bits);
	else if(load_event_bits(name, &bits) < 0)
		return;

	uint ptr = ctx->ptr;

	append(ctx, "ID_INPUT=1");

	uint tmp = ctx->ptr;

	probe_ptr(ctx, &bits);
	probe_key(ctx, &bits);

	if(ctx->ptr == tmp) /* no useful information beyond ID_INPUT=1 */
		ctx->ptr = ptr;
}

/* Only the tag added by udevd should be stored in /run/udev/data.
   Libudev will read corresponding uevent from /sys for the stuff
   that normally comes from the kernel.

   The format for these files does not match the wire messages.
   Lines are \n-terminated, values set with ENV{...} need E: prefix.
   See udevd sources.

   Naming is tricky here, cM:N (chr-major-minor) or bM:N (blk-major-minor)
   for devices or nN (net-ifi) for netdevs or else +subsystem:basename.
   Which ones libudev/libinput will try to read isn't always obvious,
   typically it's cM:N for the corresponding eventX device even when dealing
   with inputY, but sometimes it's +input:inputY. */

static void write_udev_data(CTX, char* path)
{
	int fd;
	int flags = O_WRONLY | O_CREAT | O_TRUNC;
	int mode = 0644;

	if(ctx->ptr <= ctx->sep)
		return;
	if((fd = sys_open3(path, flags, mode)) < 0)
		return;

	int len = ctx->ptr - ctx->sep;
	char* buf = ctx->uevent + ctx->sep;
	char* end = buf + len;

	int lines = 0;
	char *ls, *le;

	for(ls = buf; ls < end; ls = le + 1) {
		le = strecbrk(ls, end, '\0');
		lines++;
	}

	FMTBUF(p, e, data, len + 4*lines);

	for(ls = buf; ls < end; ls = le + 1) {
		le = strecbrk(ls, end, '\0');
		p = fmtstr(p, e, "E:");
		p = fmtraw(p, e, ls, le - ls);
		p = fmtstr(p, e, "\n");
	}

	FMTEND(p, e);

	writeall(fd, data, p - data);

	sys_close(fd);
}

static int stat_dev_input(char* name, struct stat* st)
{
	FMTBUF(p, e, path, 50);
	p = fmtstr(p, e, "/dev/input/");
	p = fmtstr(p, e, name);
	FMTEND(p, e);

	return sys_stat(path, st);
}

static void probe_device(CTX, char* name)
{
	struct stat st;

	if(stat_dev_input(name, &st) < 0)
		return;

	ctx->ptr = 0;
	ctx->sep = 0;

	probe(ctx, name);

	FMTBUF(p, e, path, 100);

	p = fmtstr(p, e, HERE "/run/udev/data");
	p = fmtstr(p, e, "/c");
	p = fmtuint(p, e, major(st.rdev));
	p = fmtstr(p, e, ":");
	p = fmtuint(p, e, minor(st.rdev));

	FMTEND(p, e);

	write_udev_data(ctx, path);
}

static void scan_devices(CTX)
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

			probe_device(ctx, de->name);
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

static void touch(char* path)
{
	int fd;
	int mode = 0644;

	if((fd = sys_open3(path, O_WRONLY | O_CREAT, mode)) < 0)
		warn(NULL, path, fd);

	sys_close(fd);
}

void init_inputs(CTX)
{
	makedir(HERE "/run/udev/");
	makedir(HERE "/run/udev/data");

	scan_devices(ctx);

	touch(HERE "/run/udev/control");
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

   Note N and M do not need to match! */

static int make_data_path(CTX, char* path, int size)
{
	char* p = path;
	char* e = path + size - 1;

	char* maj = getval(ctx, "MAJOR");
	char* min = getval(ctx, "MINOR");

	if(!maj || !min) return -1; /* inputM or something else entirely */

	p = fmtstr(p, e, HERE "/run/udev/data");
	p = fmtstr(p, e, "/c");
	p = fmtstr(p, e, maj);
	p = fmtstr(p, e, ":");
	p = fmtstr(p, e, min);

	*p = '\0';

	return 0;
}

void probe_input(CTX)
{
	char path[100];
	char* name = basename(ctx->uevent);

	probe(ctx, name);

	if(make_data_path(ctx, path, sizeof(path)) < 0)
		return;

	write_udev_data(ctx, path);
}

void clear_input(CTX)
{
	char path[100];

	if(make_data_path(ctx, path, sizeof(path)) < 0)
		return;

	sys_unlink(path);
}
