#include <bits/socket/netlink.h>
#include <sys/creds.h>
#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/dents.h>
#include <sys/signal.h>
#include <sys/timer.h>
#include <sys/inotify.h>
#include <sys/ppoll.h>
#include <sys/socket.h>

#include <format.h>
#include <string.h>
#include <util.h>
#include <main.h>

ERRTAG("findblk");

#define UDEV_MGRP_KERNEL   (1<<0)

#define CTX struct top* ctx

struct top {
	int udev;  /* socket fd */

	char* key; /* "mbr:1234ABEF", what we're looking for */
	int (*match)(CTX, char* name); /* one of the match_* functions */
	char* id;  /* "1234ABEF" in the above example */

	char** parts; /* { "1:boot", "2:root" }, partitions to link */
	int nparts;

	char device[32]; /* "sda", once it's been found */
	uint partbits;   /* which of the parts[] are still missing */
};

static const char devmapper[] = "/dev/mapper";

static int isspace(int c)
{
	return (c == ' ' || c == '\t');
}

static char* trim(char* p, char* e)
{
	while(p < e && isspace(*p)) p++;
	while(p < e && isspace(*(e-1))) e--;

	*e = '\0';

	return p;
}

static int open_device(char* name)
{
	char* pref = "/dev/";

	FMTBUF(p, e, path, 256);
	p = fmtstr(p, e, pref);
	p = fmtstr(p, e, name);
	FMTEND(p, e);

	return sys_open(path, O_RDONLY);
}

static int open_entry(char* device, char* sep, char* entry)
{
	FMTBUF(p, e, path, 256);
	p = fmtstr(p, e, "/sys/block/");
	p = fmtstr(p, e, device);
	p = fmtstr(p, e, sep);
	p = fmtstr(p, e, entry);
	FMTEND(p, e);

	return sys_open(path, O_RDONLY);
}

static int read_entry(int fd, char* buf, int len)
{
	int rd;

	rd = sys_read(fd, buf, len-1);

	sys_close(fd);

	if(rd <= 0)
		return rd;
	if(buf[rd-1] == '\n')
		rd--;

	buf[rd] = '\0';

	return rd;
}

static int load_entry(char* device, char* entry, char* buf, int len)
{
	int fd, rd;

	if((fd = open_entry(device, "/", entry)) >= 0)
		if((rd = read_entry(fd, buf, len)) > 0)
			return rd;

	if((fd = open_entry(device, "/device/", entry)) >= 0)
		if((rd = read_entry(fd, buf, len)) > 0)
			return rd;

	return 0;
}

static int compare_sys_entry(CTX, char* device, char* entry)
{
	char buf[100];
	int rd, max = sizeof(buf);

	if((rd = load_entry(device, entry, buf, max)) <= 0)
		return 0;

	char* p = trim(buf, buf + rd);

	return !strcmp(p, ctx->id);
}

static int match_name(CTX, char* name)
{
	return !strcmp(ctx->id, name);
}

static int match_cid(CTX, char* name)
{
	return compare_sys_entry(ctx, name, "cid");
}

static int match_wwid(CTX, char* name)
{
	return compare_sys_entry(ctx, name, "wwid");
}

static int match_serial(CTX, char* name)
{
	return compare_sys_entry(ctx, name, "serial");
}

static int cmple4(void* got, char* req)
{
	uint8_t* bp = got;

	FMTBUF(p, e, buf, 10);
	p = fmtbyte(p, e, bp[3]);
	p = fmtbyte(p, e, bp[2]);
	p = fmtbyte(p, e, bp[1]);
	p = fmtbyte(p, e, bp[0]);
	FMTEND(p, e);

	return strcmp(buf, req);
}

static const char mbrtag[] = { 0x55, 0xAA };

static int match_mbr(CTX, char* device)
{
	int fd, rd, ret = 0;
	char buf[0x200];

	if((fd = open_device(device)) < 0)
		return 0;
	if((rd = sys_read(fd, buf, sizeof(buf))) < 0)
		goto out;
	if((ulong)rd < sizeof(buf))
		goto out;

	if(memcmp(buf + 0x1FE, mbrtag, sizeof(mbrtag)))
		goto out;
	if(cmple4(buf + 0x1B8, ctx->id))
		goto out;

	ret = 1;
out:
	sys_close(fd);
	return ret;
}

static const char efitag[] = "EFI PART";

static int load_gpt_at(int fd, int off, void* buf, int size)
{
	int ret;

	if((ret = sys_seek(fd, off)) < 0)
		return 0;
	if((ret = sys_read(fd, buf, size)) < 0)
		return 0;
	if(ret < size)
		return 0;

	if(memcmp(buf, efitag, sizeof(efitag) - 1))
		return 0;

	return 1;
}

static char* fmtlei32(char* p, char* e, void* buf)
{
	uint8_t* v = buf;

	p = fmtbyte(p, e, v[3]);
	p = fmtbyte(p, e, v[2]);
	p = fmtbyte(p, e, v[1]);
	p = fmtbyte(p, e, v[0]);

	return p;
}

static char* fmtlei16(char* p, char* e, void* buf)
{
	uint8_t* v = buf;

	p = fmtbyte(p, e, v[1]);
	p = fmtbyte(p, e, v[0]);

	return p;
}

static int check_gpt_guid(void* buf, char* id)
{
	/* The first 4 bytes of GUID are stored as little-endian int32,
	   the following 4 as two little-endian int16-s, and the rest is
	   stored as is. Why? Because fuck you that's why! -- somebody
	   at the GPT committee, probably.  */

	FMTBUF(p, e, guid, 32 + 2);
	p = fmtlei32(p, e, buf + 0x38 + 0);
	p = fmtlei16(p, e, buf + 0x38 + 4);
	p = fmtlei16(p, e, buf + 0x38 + 6);
	p = fmtbytes(p, e, buf + 0x38 + 8, 8);
	FMTEND(p, e);

	return strncmp(guid, id, 32);
}

static int match_gpt(CTX, char* device)
{
	int fd, ret = 0;
	char buf[0x64];

	if((fd = open_device(device)) < 0)
		return 0;

	if(load_gpt_at(fd, 512, buf, sizeof(buf)))
		goto got;
	if(load_gpt_at(fd, 4096, buf, sizeof(buf)))
		goto got;
	goto out;
got:
	if(check_gpt_guid(buf, ctx->id))
		goto out;
	ret = 1;
out:
	sys_close(fd);
	return ret;
}

static int mark_partition(CTX, char* suff)
{
	int partbits = ctx->partbits;
	int i, n = ctx->nparts;
	char** parts = ctx->parts;

	int slen = strlen(suff);

	for(i = 0; i < n; i++) {
		uint mask = (1 << i);

		if(!(partbits && mask))
			continue;

		char* part = parts[i];
		char* sep = strchr(part, ':');

		if(!sep || sep == part)
			continue;

		int plen = sep - part;

		if(plen != slen)
			continue;
		if(memcmp(part, suff, plen))
			continue;

		partbits &= ~mask; /* got this partition */
	}

	ctx->partbits = partbits;

	return partbits ? -EAGAIN : 0;
}

static int check(CTX, char* name)
{
	if(ctx->match) { /* waiting for device */
		if(!ctx->match(ctx, name))
			return -EAGAIN;

		uint need = strlen(name) + 1;

		if(need > sizeof(ctx->device))
			fail("device name too long:", name, 0);

		memcpy(ctx->device, name, need);

		return 0;
	} else if(!ctx->device[0]) { /* should never happen */
		fail("attempt to match partition w/o device", NULL, 0);
	} else { /* got device, waiting for partitions */
		int nlen = strlen(name);
		int dlen = strlen(ctx->device);

		if(nlen <= dlen)
			return -EAGAIN;
		if(strncmp(name, ctx->device, dlen))
			return -EAGAIN;

		return mark_partition(ctx, name + dlen);
	}
}

static void open_udev(CTX)
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

	ctx->udev = fd;
}

static char* restof(char* line, char* pref)
{
	int plen = strlen(pref);
	int llen = strlen(line);

	if(llen < plen)
		return NULL;
	if(strncmp(line, pref, plen))
		return NULL;

	return line + plen;
}

static int recv_udev_event(CTX, char* type)
{
	int max = 1024;
	char buf[max+2];
	int rd, fd = ctx->udev;

	if((rd = sys_recv(fd, buf, max, 0)) < 0)
		fail("recv", "udev", rd);

	buf[rd] = '\0';

	char* p = buf;
	char* e = buf + rd;

	char* devtype = NULL;
	char* devname = NULL;
	char* r;

	if(strncmp(p, "add@", 4))
		return -EAGAIN; /* ignore non-add events */

	while(p < e) {
		if((r = restof(p, "DEVTYPE=")))
			devtype = r;
		if((r = restof(p, "DEVNAME=")))
			devname = r;

		p += strlen(p) + 1;

		if(devtype && devname)
			break;
	}

	if(!devtype || !devname)
		return -EAGAIN;
	if(strcmp(devtype, type))
		return -EAGAIN;

	return check(ctx, devname);
}

static int wait_devices(CTX, struct timespec* ts, char* type)
{
	int ret;
	struct pollfd pfd = {
		.fd = ctx->udev,
		.events = POLLIN
	};
poll:
	if((ret = sys_ppoll(&pfd, 1, ts, NULL)) < 0)
		fail("ppoll", "udev", ret);
	if(ret == 0)
		return -ETIMEDOUT;

	if((ret = recv_udev_event(ctx, type)) < 0)
		goto poll;

	return ret;
}

static int scan_devices(CTX, const char* dir)
{
	int fd, rd, ret;
	char buf[1024];

	if((fd = sys_open(dir, O_DIRECTORY)) < 0)
		fail(NULL, dir, fd);

	while((rd = sys_getdents(fd, buf, sizeof(buf))) > 0) {
		void* p = buf;
		void* e = buf + rd;

		while(p < e) {
			struct dirent* de = p;

			if(!de->reclen)
				continue;

			p += de->reclen;

			if(dotddot(de->name))
				continue;

			if((ret = check(ctx, de->name)) >= 0)
				goto out;
		}
	}

	ret = -ENOENT;
out:
	sys_close(fd);

	return ret;
}

static void locate_device(CTX)
{
	struct timespec ts;

	if(scan_devices(ctx, "/sys/block") >= 0)
		return;

	ts.sec = 1;
	ts.nsec = 0;

	if(wait_devices(ctx, &ts, "disk") >= 0)
		return;

	warn("waiting for", ctx->key, 0);
	ts.sec = 4;
	ts.nsec = 0;

	if(wait_devices(ctx, &ts, "disk") >= 0)
		return;

	fail("timed out", NULL, 0);
}

static void report_missing_partitions(CTX)
{
	int i, n = ctx->nparts;
	int partbits = ctx->partbits;

	warn("found device", ctx->id, 0);

	for(i = 0; i < n; i++) {
		if(!(partbits & (1<<i)))
			continue;
		warn("missing partition", ctx->parts[i], 0);
	}

	_exit(0xFF);
}

static void locate_parts(CTX)
{
	struct timespec ts = { 1, 0 };

	ctx->match = NULL;

	if(!ctx->partbits)
		return;

	FMTBUF(p, e, path, 100);
	p = fmtstr(p, e, "/sys/block/");
	p = fmtstr(p, e, ctx->device);
	FMTEND(p, e);

	if(scan_devices(ctx, path) >= 0)
		return;
	if(wait_devices(ctx, &ts, "partition") >= 0)
		return;

	report_missing_partitions(ctx);
}

static void link_part(CTX, char* suff, int slen, char* label)
{
	int ret;

	FMTBUF(lp, le, link, 200);
	lp = fmtstr(lp, le, devmapper);
	lp = fmtstr(lp, le, "/");
	lp = fmtstr(lp, le, label);
	FMTEND(lp, le);

	FMTBUF(bp, be, node, 200);
	bp = fmtstr(bp, be, "../");
	bp = fmtstr(bp, be, ctx->device);
	bp = fmtstrn(bp, be, suff, slen);
	FMTEND(bp, be);

	if((ret = sys_symlink(node, link)) < 0)
		fail(NULL, link, ret);
}

static void link_parts(CTX)
{
	char** parts = ctx->parts;
	int i, nparts = ctx->nparts;
	const char* dir = devmapper;
	int ret;

	if(((ret = sys_mkdir(dir, 0755)) < 0) && (ret != -EEXIST))
		fail(NULL, dir, ret);

	for(i = 0; i < nparts; i++) {
		char* part = parts[i];
		char* sep = strchr(part, ':');

		if(sep)
			link_part(ctx, part, sep - part, sep + 1);
		else if(i > 0)
			fail("attempt to link invalid part", part, 0);
		else
			link_part(ctx, "", 0, part); /* whole device */
	}
}

static void set_match_key(CTX, char* key)
{
	char* sep;

	if(!(sep = strchr(key, ':')))
		fail("invalid device spec:", key, 0);

	int len = sep - key;

	if(!strncmp(key, "name", len))
		ctx->match = match_name;
	else if(!strncmp(key, "mbr", len))
		ctx->match = match_mbr;
	else if(!strncmp(key, "gpt", len))
		ctx->match = match_gpt;
	else if(!strncmp(key, "cid", len))
		ctx->match = match_cid;
	else if(!strncmp(key, "wwid", len))
		ctx->match = match_wwid;
	else if(!strncmp(key, "serial", len))
		ctx->match = match_serial;
	else
		fail("invalid device spec:", key, 0);

	ctx->id = sep + 1;
	ctx->key = key;
}

static void setup_context(CTX, int argc, char** argv)
{
	if(argc < 3)
		fail("too few arguments", NULL, 0);

	set_match_key(ctx, argv[1]);

	for(int i = 3; i < argc; i++)
		if(!strchr(argv[i], ':'))
			fail("invalid part spec:", argv[i], 0);

	char** parts = argv + 2;
	int nparts = argc - 2;

	if(nparts > 31)
		fail("too many partitions", NULL, 0);

	uint partbits = (1 << nparts) - 1;

	if(!strchr(parts[0], ':'))
		partbits &= ~1; /* not a partition */

	ctx->parts = parts;
	ctx->nparts = nparts;
	ctx->partbits = partbits;
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	memzero(ctx, sizeof(*ctx));
	setup_context(ctx, argc, argv);

	open_udev(ctx);
	locate_device(ctx);
	locate_parts(ctx);
	link_parts(ctx);

	return 0;
}
