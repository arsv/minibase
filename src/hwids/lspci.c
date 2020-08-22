#include <sys/file.h>
#include <sys/dents.h>
#include <sys/mman.h>

#include <format.h>
#include <string.h>
#include <output.h>
#include <util.h>
#include <main.h>

ERRTAG("lspci");

/* See comments in lsusb.c. PCI device classes are much easier to use though.
   In most PCI systems, driver name would also be meaningful. We use that,
   effectively leveraging the implicit PCI id database embedded in kernel
   modules. It's useless for unsupported device, but helps a lot to exclude
   known devices. */

struct dev {
	char slot[12];   /* "0000:00:1f.6" */
	char driver[20]; /* "e1000e" */
	int id[2];       /* 0x8086, 0x156F */
	int class;       /* 0x20000 */
};

struct top {
	void* brk;
	void* ptr;
	void* end;

	int count;
	struct dev** idx;

	struct bufout bo;
};

#define CTX struct top* ctx __unused

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

static int parse_hex(char* line, int* dst)
{
	char* p;

	if(!(p = parsehex(line, dst)) || *p)
		return -1;

	return 0;
}

static int parse_hex2(char* line, int* dst)
{
	char* p = line;

	if(!(p = parsehex(p, &dst[0])) || *p++ != ':')
		return -1;
	if(!(p = parsehex(p, &dst[1])) || *p)
		return -1;

	return 0;
}

static int parse_drv(char* line, struct dev* dev)
{
	int len = strlen(line);
	int max = sizeof(dev->driver) -1;

	if(len > max) len = max;

	memcpy(dev->driver, line, len);

	dev->driver[len] = '\0';

	return 0;
}

/* Typical contents of /sys/bus/pci/devices/0000:00:1f.6/uevent:

	DRIVER=e1000e
	PCI_CLASS=20000
	PCI_ID=8086:156F
	PCI_SUBSYS_ID=1028:06DE
	PCI_SLOT_NAME=0000:00:1f.6
	MODALIAS=pci:v00008086d0000156Fsv00001028sd000006DEbc02sc00i00

   All numbers are in hex. We skip SUBSYS, and do not use the lower two
   bytes of CLASS. */

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

		if((v = prefixed(p, "DRIVER=")))
			ret = parse_drv(v, dev);
		else if((v = prefixed(p, "PCI_CLASS=")))
			ret = parse_hex(v, &dev->class);
		else if((v = prefixed(p, "PCI_ID=")))
			ret = parse_hex2(v, dev->id);
		else
			goto next;

		if(ret < 0)
			return ret;

		next: p = q + 1;
	}

	return 0;
}

static int read_uevent(CTX, int at, struct dev* dev)
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

/* Here at is /sys/bus/pci/devices and name="0000:00:1f.6". */

static void scan_entry(CTX, int at, char* name)
{
	void* ptr = ctx->ptr;
	struct dev* dev = heap_alloc(ctx, sizeof(*dev));
	int fd;

	memzero(dev, sizeof(*dev));

	if(strlen(name) != sizeof(dev->slot))
		return;

	memcpy(dev->slot, name, sizeof(dev->slot));

	if((fd = sys_openat(at, name, O_DIRECTORY)) < 0)
		return;

	if(read_uevent(ctx, fd, dev) >= 0)
		ctx->count++;
	else
		ctx->ptr = ptr;

	sys_close(fd);
}

static void read_sys_bus_pci(CTX)
{
	char buf[1024];
	int fd, rd;
	char* dir = "/sys/bus/pci/devices";

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

			scan_entry(ctx, fd, de->name);
		}
	}

	sys_close(fd);
}

/* Sorting by slot ("00:1f.6"). */

static int cmpdev(void* a, void* b)
{
	struct dev* da = a;
	struct dev* db = b;

	return memcmp(da->slot, db->slot, sizeof(da->slot));
}

static void scan_devices(CTX)
{
	void* ptr = ctx->ptr;

	ctx->count = 0;
	read_sys_bus_pci(ctx);

	void* end = ctx->ptr;

	int count = ctx->count;
	struct dev** idx = heap_alloc(ctx, (count+1)*sizeof(void*));

	void* p = ptr;
	int i = 0;

	while(p < end) {
		struct dev* d = (struct dev*) p;
		idx[i++] = d;
		p += sizeof(*d);
	}

	qsortp(idx, count, cmpdev);

	ctx->idx = idx;
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

static char* fmt_slot(char* p, char* e, struct dev* dev)
{
	char* q = dev->slot;
	char* z = q + sizeof(dev->slot);

	while(*q == '0' && q < z)
		q++;
	if(q < z && *q == ':')
		q++;

	return fmtstrn(p, e, q, z - q);
}

static char* fmt_class(char* p, char* e, int class)
{
	int top = (class >> 16) & 0xFF;
	char* name = NULL;

	switch(top) {
		case 0x01: name = "mass storage"; break;
		case 0x02: name = "network controller"; break;
		case 0x03: name = "display controller"; break;
		case 0x04: name = "multimedia controller"; break;
		case 0x05: name = "memory controller"; break;
		case 0x06: name = "bridge"; break;
		case 0x07: name = "communication controller"; break;
		case 0x08: name = "generic system peripheral"; break;
		case 0x09: name = "input device controller"; break;
		case 0x0a: name = "docking station"; break;
		case 0x0b: name = "processor"; break;
		case 0x0c: name = "serial bus controller"; break;
		case 0x0d: name = "wireless controller"; break;
		case 0x0e: name = "intelligent controller"; break;
		case 0x0f: name = "satcomm controller"; break;
		case 0x10: name = "encryption controller"; break;
		case 0x11: name = "signal processing controller"; break;
		case 0x12: name = "processing accelerators"; break;
		case 0x13: name = "non-essential instrumentation"; break;
		case 0x40: name = "coprocessor"; break;
		case 0xff: name = "unassigned class"; break;
		default:
			p = fmtstr(p, e, "class 0x");
			p = fmtbyte(p, e, top);
			return p;
	}

	return fmtstr(p, e, name);
}

/* Output follows lsusb, with leading vendor:product pair because it's
   fixed width.

       8086:156F 00:1f.6 network controller (e1000e)

   In the vast majority of cases the address will always be 0000: so we skip
   it. However, it's there for a reason, so who knows maybe in some systems
   it may in fact be nonzero. */

static void format_dev(CTX, struct dev* dev)
{
	FMTBUF(p, e, buf, 300);

	p = fmtpad0(p, e, 4, fmthex(p, e, dev->id[0]));
	p = fmtstr(p, e, ":");
	p = fmtpad0(p, e, 4, fmthex(p, e, dev->id[1]));

	p = fmtstr(p, e, " ");
	p = fmt_slot(p, e, dev);

	p = fmtstr(p, e, " ");
	p = fmt_class(p, e, dev->class);

	if(dev->driver[0]) {
		p = fmtstr(p, e, " (");
		p = fmtstrn(p, e, dev->driver, sizeof(dev->driver));
		p = fmtstr(p, e, ")");
	}

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
	(void)argv;
	struct top context, *ctx = &context;

	if(argc > 1)
		fail("too many arguments", NULL, 0);

	memzero(ctx, sizeof(*ctx));
	init_heap(ctx);

	scan_devices(ctx);
	dump_devices(ctx);

	return 0;
}
