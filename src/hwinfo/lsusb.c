#include <sys/file.h>
#include <sys/dents.h>
#include <sys/mman.h>

#include <errtag.h>
#include <format.h>
#include <string.h>
#include <output.h>
#include <util.h>

ERRTAG("lsusb");

/* The purpose of this tool is to let the user figure which devices are
   sitting on the USB bus, and how Linux identifies them internally.

   Unlike the conventional lsusb from usbtools package, this one does not
   rely on usb.ids database (www.linux-usb.org/usb.ids). Instead, it uses
   the data provided by the device itself (device class, interface classes
   and mfgr strings). This turns out to be more than enough to identify
   devices in most cases, either directly or by exclusion, and removes
   the need to carry rather large usb.ids around. The usefullness of that
   file is questionable either way, it's rather incomplete.
 
   In most cases, the *device* class is useless and we must look up
   interface classes on each endpoint. */

struct dev {
	int len;

	int busnum;
	int devnum;

	int vendor;
	int product;

	uint ncls;
	byte cls[8];

	char payload[];
};

struct top {
	void* brk;
	void* ptr;
	void* end;

	int count;
	struct dev** idx;

	struct bufout bo;
};

#define CTX struct top* ctx

static void init_heap(CTX)
{
	void* brk = sys_brk(0);
	void* end = sys_brk(brk + PAGE);

	if(brk_error(brk, end))
		fail("cannot allocate memory", NULL, 0);

	ctx->brk = brk;
	ctx->ptr = brk;
	ctx->end = end;
}

static void heap_extend(CTX, long need)
{
	need += (PAGE - need % PAGE) % PAGE;
	char* req = ctx->end + need;
	char* new = sys_brk(req);

	if(mmap_error(new))
		fail("cannot allocate memory", NULL, 0);

	ctx->end = new;
}

static void* heap_alloc(CTX, int len)
{
	void* ptr = ctx->ptr;
	long avail = ctx->end - ptr;

	if(avail < len)
		heap_extend(ctx, len - avail);

	ctx->ptr += len;

	return ptr;
}

static long heap_left(CTX)
{
	return ctx->end - ctx->ptr;
}

static char* prefixed(char* line, char* pref)
{
	char* a = line;
	char* b = pref;

	while(*a && *b)
		if(*a == *b) {
			a++;
			b++;
		} else {
			break;
		}

	if(*a && !*b)
		return a;
	else
		return NULL;
}

static int parse_int(char* line, int* dst)
{
	char* p;

	if(!(p = parseint(line, dst)) || *p)
		return -1;

	return 0;
}

static int parse_product(char* line, struct dev* dev)
{
	char* p = line;

	if(!(p = parsehex(p, &dev->vendor)) || *p++ != '/')
		return -1;
	if(!(p = parsehex(p, &dev->product)) || *p++ != '/')
		return -1;

	return 0;
}

static int parse_type(char* line, struct dev* dev)
{
	char* p = line;
	int val;

	if(!(p = parseint(p, &val)) || *p++ != '/')
		return -1;

	dev->cls[0] = val;
	dev->ncls = 1;

	return 0;
}

/* Typical contents of /sys/bus/usb/devices/1-7/uevent:

	MAJOR=189
	MINOR=2
	DEVNAME=bus/usb/001/003
	DEVTYPE=usb_device
	DRIVER=usb
	PRODUCT=1bcf/28b8/5413
	TYPE=239/2/1
	BUSNUM=001
	DEVNUM=003

   In the functions below, at referes to /sys/bus/usb/devices/1-7
   and name is "1-7". */ 

static int parse_uevent(CTX, struct dev* dev, char* buf, int len)
{
	char* p = buf;
	char* e = buf + len;
	char *q, *v;
	int ret;

	while(p < e) {
		if((q = strecbrk(p, e, '\n')) >= e)
			break;
		*q = '\0';

		if((v = prefixed(p, "PRODUCT=")))
			ret = parse_product(v, dev);
		else if((v = prefixed(p, "BUSNUM=")))
			ret = parse_int(v, &dev->busnum);
		else if((v = prefixed(p, "DEVNUM=")))
			ret = parse_int(v, &dev->devnum);
		else if((v = prefixed(p, "TYPE=")))
			ret = parse_type(v, dev);
		else
			goto next;

		if(ret < 0)
			return ret;

		next: p = q + 1;
	}

	return 0;
}

static int read_uevent(CTX, int at, char* name, struct dev* dev)
{
	int fd, rd;
	int ret = -1;
	char buf[1024];

	if((fd = sys_openat(at, "uevent", O_RDONLY)) < 0)
		return ret;

	if((rd = sys_read(fd, buf, sizeof(buf))) <= 0)
		;
	else if(parse_uevent(ctx, dev, buf, rd) >= 0)
		ret = 0;

	sys_close(fd);

	return ret;
}

/* Device payload carries two 0-terminated strings, the name and
   device mfgr/product id, like this: "1-7₀Broadcom Corp 5880₀". */

static void append_payload(CTX, char* data, int len)
{
	char* buf = heap_alloc(ctx, len);
	memcpy(buf, data, len);
}

static char* read_entry(char* p, char* e, int at, char* relname)
{
	int fd, rd;

	if(e - p < 1)
		return p;
	if((fd = sys_openat(at, relname, O_RDONLY)) < 0)
		return p;
	if((rd = sys_read(fd, p, e - p)) < 0)
		goto out;

	if(rd > 0 && p[rd-1] == '\n') rd--;

	p += rd;
out:
	sys_close(fd);

	return p;
}

/* Files like /sys/bus/usb/devices/1-7:0.2/bInterfaceNumber contain
   a single integer, some in dec some in hex. */

static int read_int_file(int at, char* name, int hex)
{
	int fd, rd, val = 0;
	char buf[50];

	if((fd = sys_openat(at, name, O_RDONLY)) < 0)
		return fd;

	rd = sys_read(fd, buf, sizeof(buf)-1);

	sys_close(fd);

	if(rd <= 0)
		return -EINVAL;

	buf[rd] = '\0';

	char* p = buf;
	char* e = buf + rd;

	while(p < e && *p == ' ') p++;

	if(hex)
		p = parsehex(p, &val);
	else
		p = parseint(p, &val);

	if(*p && *p != '\n')
		return -EINVAL;

	return val;
}

/* We are only interested in unique cls values. In many cases, there are
   several endpoints with the same class. But there are lots of devices
   with several unique classes on different endpoints. */

static void add_dev_class(struct dev* dev, int cls)
{
	uint i;

	for(i = 0; i < dev->ncls; i++)
		if(dev->cls[i] == cls)
			return;

	dev->cls[dev->ncls++] = cls;
}

/* Endpoint naming scheme: hub-port:config.iface */

static void read_iface_classes(CTX, int at, char* name, struct dev* dev)
{
	int config, niface, fd, cls;

	if((config = read_int_file(at, "bConfigurationValue", 0)) < 0)
		return;
	if((niface = read_int_file(at, "bNumInterfaces", 0)) <= 0)
		return;

	for(int i = 0; i < niface; i++) {
		FMTBUF(p, e, path, 50);
		p = fmtstr(p, e, name);
		p = fmtstr(p, e, ":");
		p = fmtint(p, e, config);
		p = fmtstr(p, e, ".");
		p = fmtint(p, e, i);
		FMTEND(p, e);

		if((fd = sys_openat(at, path, O_DIRECTORY)) < 0)
			break;

		if((cls = read_int_file(fd, "bInterfaceClass", 1)) > 0)
			add_dev_class(dev, cls);

		sys_close(fd);

		if(dev->ncls >= ARRAY_SIZE(dev->cls))
			break;
	}
}

/* Product and manufacturer names come from the device and may contain
   arbitrary garbage. Also all files in sysfs are \n-terminated. */

void sanitize(char* s, char* p)
{
	for(; s < p; s++)
		if(*s < 0x20)
			*s = '?';
}

static void read_prodname(CTX, int at, char* name, struct dev* dev)
{
	FMTBUF(p, e, buf, 100);
	p = read_entry(p, e, at, "manufacturer");
	if(p > buf && p < e) *p++ = ' ';
	p = read_entry(p, e, at, "product");
	FMTEND(p, e);

	sanitize(buf, p);

	append_payload(ctx, buf, p - buf + 1);
}

/* Here at is /sys/bus/usb/devices and name="1-7". */

static void scan_entry(CTX, int at, char* name)
{
	void* ptr = ctx->ptr;
	struct dev* dev = heap_alloc(ctx, sizeof(*dev));
	int fd;

	memzero(dev, sizeof(*dev));

	if((fd = sys_openat(at, name, O_DIRECTORY)) < 0)
		return;

	if(read_uevent(ctx, fd, name, dev)) {
		ctx->ptr = ptr;
		goto out;
	}

	append_payload(ctx, name, strlen(name) + 1);

	read_prodname(ctx, fd, name, dev);

	read_iface_classes(ctx, fd, name, dev);

	int len = ctx->ptr - ptr;
	int pad = 4 - len % 4;
	
	(void)heap_alloc(ctx, pad);

	dev->len = len + pad;
	ctx->count++;
out:
	sys_close(fd);
}

/* /sys/bus/usb/devices lists devices proper (1-7), endpoints (1-7:0.1)
   and hubs (usb1). We are only interested in devices. */

static int isdigit(int c)
{
	return (c >= '0' && c <= '9');
}

static int node_name(char* name)
{
	if(!isdigit(*name))
		return 0;
	if(strchr(name, ':'))
		return 0;

	return 1;
}

static void read_sys_bus_usb(CTX)
{
	char buf[1024];
	int fd, rd;
	char* dir = "/sys/bus/usb/devices";

	if((fd = sys_open(dir, O_DIRECTORY)) < 0)
		fail(NULL, dir, fd);

	while((rd = sys_getdents(fd, buf, sizeof(buf))) > 0) {
		char* p = buf;
		char* e = buf + rd;

		while(p < e) {
			struct dirent* de = (struct dirent*) p;

			p += de->reclen;

			if(dotddot(de->name))
				continue;
			if(!node_name(de->name))
				continue;

			scan_entry(ctx, fd, de->name);
		}
	}

	sys_close(fd);
}

/* Node names have some physical significance apparently, so let's use
   them for sorting. Also it's the primary way to refer to device in
   kernel logs.

   Bus:dev pairs are volatile and generally change on replugging the dev. */

static int cmpdev(const void* a, const void* b)
{
	struct dev* da = *((struct dev**)a);
	struct dev* db = *((struct dev**)b);

	return natcmp(da->payload, db->payload);
}

static void scan_devices(CTX)
{
	void* ptr = ctx->ptr;

	ctx->count = 0;
	read_sys_bus_usb(ctx);

	void* end = ctx->ptr;

	int count = ctx->count;
	struct dev** idx = heap_alloc(ctx, (count+1)*sizeof(void*));

	void* p = ptr;
	int i = 0;

	while(p < end) {
		struct dev* d = (struct dev*) p;
		idx[i++] = d;
		p += d->len;
	}

	qsort(idx, count, sizeof(void*), cmpdev);

	ctx->idx = idx;
}

/* Ref. http://www.usb.org/developers/defined_class
 
   Subclass and protocol numbers are of little use for dev identification.
   Either the class is enough, or it needs a vendor:product lookup. */

static const char* usb_class_name(int class)
{
	switch(class) {
		case 0x00: return "";
		case 0x01: return "audio";
		case 0x02: return "cdc";
		case 0x03: return "hid";
		case 0x05: return "physical";
		case 0x06: return "image";
		case 0x07: return "printer";
		case 0x08: return "storage";
		case 0x09: return "hub";
		case 0x0A: return "cdc-data";
		case 0x0B: return "smartcard";
		case 0x0D: return "content security";
		case 0x0E: return "video";
		case 0x10: return "audio/video";
		case 0x11: return "billboard";
		case 0x12: return "usb-c bridge";
		case 0xDC: return "diagnostics";
		case 0xE0: return "wireless";
		case 0xEF: return "";
		case 0xFE: return "";
		case 0xFF: return "";
		default: return NULL;
	}
}

static char* describe_class(char* p, char* e, struct dev* dev)
{
	char* s = p;
	p = fmtstr(p, e, " (");
	char* q = p;
	uint i;

	for(i = 0; i < dev->ncls; i++) {
		int class = dev->cls[i];
		const char* name = usb_class_name(class);

		if(name && !*name)
			continue;
		if(p > q)
			p = fmtstr(p, e, ",");
		if(name) {
			p = fmtstr(p, e, name);
		} else {
			p = fmtstr(p, e, "0x");
			p = fmthex(p, e, class);
		}
	}

	if(p <= q) return s;

	p = fmtstr(p, e, ")");

	return p;
}

static void prep_output(CTX)
{
	int est = 100*ctx->count;
	int left = heap_left(ctx);

	if(left < est)
		heap_extend(ctx, PAGE);

	int len = heap_left(ctx);
	void* buf = heap_alloc(ctx, len);

	ctx->bo.fd = STDOUT;
	ctx->bo.buf = buf;
	ctx->bo.len = len;
}

static void output(CTX, char* buf, int len)
{
	bufout(&ctx->bo, buf, len);
}

/* Output format is chosen mostly to keep them naturally aligned:

       0C5A:5085 001.004 1-7 Broadcom .... (class)

   The "1-7" part can be much longer than that, especially if intermediate
   hubs are involved (1-5-27). */

static void format_dev(CTX, struct dev* dev)
{
	char* payload = dev->payload;

	FMTBUF(p, e, buf, 300);

	e -= 100;

	p = fmtpad0(p, e, 4, fmthex(p, e, dev->vendor));
	p = fmtstr(p, e, ":");
	p = fmtpad0(p, e, 4, fmthex(p, e, dev->product));

	p = fmtstr(p, e, " ");
	p = fmtpad0(p, e, 3, fmtint(p, e, dev->busnum));
	p = fmtstr(p, e, ".");
	p = fmtpad0(p, e, 3, fmtint(p, e, dev->devnum));

	p = fmtstr(p, e, " ");
	p = fmtstr(p, e, payload);
	payload += strlen(payload) + 1;

	if(*payload) {
		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, payload);
	}

	e += 100;

	p = describe_class(p, e, dev);

	FMTENL(p, e);

	output(ctx, buf, p - buf);
}

static void dump_devices(CTX)
{
	int i, count = ctx->count;

	prep_output(ctx);

	for(i = 0; i < count; i++)
		format_dev(ctx, ctx->idx[i]);

	bufoutflush(&ctx->bo);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	if(argc > 1)
		fail("too many arguments", NULL, 0);

	memzero(ctx, sizeof(*ctx));
	init_heap(ctx);

	scan_devices(ctx);
	dump_devices(ctx);

	return 0;
}
