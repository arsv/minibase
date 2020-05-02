#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/mman.h>
#include <sys/splice.h>

#include <format.h>
#include <output.h>
#include <string.h>
#include <util.h>

#include "cpio.h"

/* 070701 = regular ASCII CPIO, 070702 = with checksums.
   Same headers, the checksummed one has header.check fields filled. */

static void check_magic(CTX, char magic[6])
{
	if(!memcmp(magic, "070701", 6))
		return;
	if(!memcmp(magic, "070702", 6))
		return;

	fatal(ctx, "invalid header magic");
}

/* ASCII CPIO uses %08x format in fixed 8-byte-wide fields for 32-bit
   numeric values like file sizes. */

static int parse_size(CTX, char str[8], uint* out)
{
	uint r = 0;
	int i;

	for(i = 0; i < 8; i++) {
		char ci = str[i];
		int di;

		if(ci >= '0' && ci <= '9')
			di = (ci - '0');
		else if(ci >= 'a' && ci <= 'f')
			di = 10 + (ci - 'a');
		else if(ci >= 'A' && ci <= 'F')
			di = 10 + (ci - 'A');
		else
			fatal(ctx, "invalid size");

		r = (r << 4) | di;
	};

	*out = r;

	return 0;
}

/* The header is 110 bytes and the name field can be up to a PAGE long.
   We want current header to be completely in the memory, including its
   name field, which is achieved by buffering two pages from the stream
   and shifting the buffer so that the header is right at the start of
   the buffer. The trailing data read alongside the header gets dumped
   into the output file later.

   Note it is not unusual to have something like

     "header contents header contents header conte..."

   within the two page span, especially with symlinks and such.
   In such a case, we want to avoid and extra read and just use
   the data that's in the buffer already. */

static int shift_buffer(CTX)
{
	void* head = ctx->head;
	int hptr = ctx->hptr;
	int hend = ctx->hend;

	if(hptr >= hend) {
		ctx->hptr = 0;
		ctx->hend = 0;
		return 0;
	}

	int left = hend - hptr;
	void* tail = head + hptr;

	memmove(head, tail, left);

	ctx->hptr = 0;
	ctx->hend = left;

	return left;
}

static int grab_cached(CTX, int size)
{
	int hptr = ctx->hptr;
	int hend = ctx->hend;

	if(hptr >= hend)
		return 0;

	int left = hend - hptr;

	if(left > size)
		left = size;

	ctx->hptr += left;

	return left;
}

static int fill_buffer(CTX)
{
	int fd = ctx->fd;
	int ret;

	int hlen = ctx->hlen;
	int hend = ctx->hend;

	if(hend >= hlen)
		fail("out of buffer space", NULL, 0);

	int left = hlen - hend;
	void* buf = ctx->head + hend;

	if((ret = sys_read(fd, buf, left)) < 0)
		fail("read", NULL, ret);

	ctx->hend = hend + ret;

	return ret;
}

/* Load the next header, without the name yet, into ctx->head.
   NULL return here indicates a normal EOF. If it's not EOF, there
   should be at least a complete header there. */

static struct header* read_head(CTX)
{
	int got = shift_buffer(ctx);
	int skip = ctx->skip;
	int size = sizeof(struct header);
	int need = size + skip;

	ctx->skip = 0;

	ctx->off += ctx->rec;
	ctx->rec = 0;

	if(got < need)
		(void)fill_buffer(ctx);

	got = grab_cached(ctx, need);

	if(!got)
		return NULL;
	if(got < need)
		fail("truncated header", NULL, 0);

	ctx->rec = size;

	return ctx->head + skip;
}

static void read_name(CTX, uint namesize)
{
	if(namesize > PAGE)
		fatal(ctx, "entry name too long");

	int aligned = align4(2 + namesize) - 2;

	int hptr = ctx->hptr;
	int hend = ctx->hend;
	int left = (hptr >= hend) ? 0 : (hend - hptr);

	if(left < aligned)
		(void)fill_buffer(ctx);

	int got = grab_cached(ctx, aligned);

	if(got < aligned)
		fatal(ctx, "truncated entry name");

	ctx->rec += aligned;
}

/* Make sure the namesize bytes following the header do contain
   a properly terminated file name. */

static int check_name(CTX, char* name, int namesize)
{
	if(namesize > PAGE)
		fatal(ctx, "entry name too long");

	char* p = name;
	char* e = p + namesize;

	for(; p < e; p++)
		if(!*p) break;
	if(p >= e)
		fatal(ctx, "invalid entry name");
	if(p + 1 < e)
		fatal(ctx, "null chars in name");

	return strncmp(name, "TRAILER!!!", namesize);
}

/* Make sure names in the archive are always taken relative to the
   directory we are unpacking them into. This means stripping
   the leading / if any, and making sure no "/../" (dotcount=2) fragments.
   We also reject "/./" (dotcount=1). */

static char* validate_name(CTX, char* name)
{
	int dotcount = 0;
	char *p, c;

	if(*name == '/')
		name++;

	for(p = name; (c = *p); p++) {
		if(c == '.') {
			if(dotcount >= 3)
				;
			else if(dotcount < 0)
				;
			else dotcount++;

		} else if(c != '/') {
			dotcount = -1;
		} else if(dotcount == 1 || dotcount == 2) {
			fatal(ctx, "invalid entry path");
		} else { /* valid "/" dir separator */
			dotcount = 0;
		}
	}

	return name;
}

static int dev_null_fd(CTX)
{
	char* name = "/dev/null";
	int fd = ctx->null;

	if(fd > 0)
		return fd;

	if((fd = sys_open(name, O_WRONLY)) < 0)
		fail(NULL, name, fd);

	ctx->null = fd;

	return fd;
}

static void stream_rest(CTX, int fd, uint left)
{
	int ifd = ctx->fd;
	int ofd = fd;
	int ret;

	while(left > 0) {
		int sfb = (left < (1U<<30)) ? left : (1U<<30);

		if((ret = sys_sendfile(ofd, ifd, NULL, sfb)) < 0)
			fail("sendfile", NULL, ret);
		else if(ret == 0)
			break;

		left -= ret;
	}
}

static void skip_file(CTX, uint filesize)
{
	uint aligned = align4(filesize);

	int hlen = ctx->hlen;
	int block = (aligned < (uint)hlen) ? aligned : hlen;

	int got = grab_cached(ctx, block);

	if(got >= aligned)
		return;

	stream_rest(ctx, dev_null_fd(ctx), aligned - got);
}

static int list_single_entry(CTX, struct header* hdr, struct bufout* bo)
{
	uint filesize;
	uint namesize;

	check_magic(ctx, hdr->magic);
	parse_size(ctx, hdr->filesize, &filesize);
	parse_size(ctx, hdr->namesize, &namesize);

	read_name(ctx, namesize);

	ctx->rec += align4(filesize);

	char* name = hdr->name;

	if(!check_name(ctx, name, namesize))
		return 0;

	bufout(bo, name, namesize);
	bufout(bo, "\n", 1);

	skip_file(ctx, filesize);

	return 1;
}

static void read_list_entries(CTX, struct bufout* bo)
{
	struct header* hdr;

	while((hdr = read_head(ctx)))
		if(!list_single_entry(ctx, hdr, bo))
			return;
}

static void remove_existing(CTX, char* name)
{
	int ret, at = ctx->at;

	if((ret = sys_unlinkat(at, name, 0)) >= 0)
		;
	else if(ret != -ENOENT && ret != -ENOTDIR)
		failx(ctx, name, ret);
}

static int open_output(CTX, char* name, int mode)
{
	int fd, at = ctx->at;
	int flags = O_WRONLY | O_CREAT | O_EXCL;

	mode = (mode & 0111) ? 0755 : 0644;

	remove_existing(ctx, name);

	if((fd = sys_openat4(at, name, flags, mode)) < 0)
		failx(ctx, name, fd);

	return fd;
}

static int write_cached(CTX, int fd, uint filesize)
{
	int hlen = ctx->hlen;
	int block = (filesize < (uint)hlen) ? filesize : hlen;

	void* tail = ctx->head + ctx->hptr;
	int got = grab_cached(ctx, block);

	if(!got) return got;

	int ret;

	if((ret = sys_write(fd, tail, got)) < 0)
		fail("write", NULL, ret);
	if(ret != got)
		fail("incomplete write", NULL, 0);

	return ret;
}

static void extract_file(CTX, uint filesize, char* name, uint mode)
{
	int fd = open_output(ctx, name, mode);
	int got = write_cached(ctx, fd, filesize);

	if(got >= filesize)
		return;

	stream_rest(ctx, fd, filesize - got);
}

static int copy_cached(CTX, void* buf, int len)
{
	void* tail = ctx->head + ctx->hptr;
	int got = grab_cached(ctx, len);

	if(!got) return got;

	memcpy(buf, tail, got);

	return got;
}

static void read_content(CTX, void* buf, uint size)
{
	int fd = ctx->fd;
	int ret;

	if((ret = sys_read(fd, buf, size)) < 0)
		fail("read", NULL, ret);
	if(ret != size)
		fatal(ctx, "truncated symlink");
}

static void extract_link(CTX, uint filesize, char* name)
{
	if(filesize > PAGE)
		fatal(ctx, "symlink too long");

	char* buf = alloca(filesize + 1);
	int got = copy_cached(ctx, buf, filesize);

	if(got < filesize)
		read_content(ctx, buf + got, filesize - got);

	buf[filesize] = '\0';

	int ret, at = ctx->at;

	remove_existing(ctx, name);

	if((ret = sys_symlinkat(buf, at, name)) < 0)
		failx(ctx, name, ret);
}

static void extract_dir(CTX, char* name)
{
	int at = ctx->at;
	int ret;

	if((ret = sys_mkdirat(at, name, 0755)) >= 0)
		return;
	if(ret == -EEXIST)
		return;

	failx(ctx, name, ret);
}

static int extract_single_entry(CTX, struct header* hdr)
{
	uint namesize;
	uint filesize;
	uint mode;

	check_magic(ctx, hdr->magic);
	parse_size(ctx, hdr->filesize, &filesize);
	parse_size(ctx, hdr->namesize, &namesize);
	parse_size(ctx, hdr->mode, &mode);

	read_name(ctx, namesize);

	ctx->skip = align4(filesize) - filesize;
	ctx->rec += align4(filesize);

	char* name = hdr->name;

	if(!check_name(ctx, name, namesize))
		return 0;

	name = validate_name(ctx, name);

	int type = mode & S_IFMT;

	if(type == S_IFREG)
		extract_file(ctx, filesize, name, mode);
	else if(type == S_IFLNK)
		extract_link(ctx, filesize, name);
	else if(filesize != 0)
		fatal(ctx, "non-zero size in non-regular file");
	else if(type == S_IFDIR)
		extract_dir(ctx, name);

	return 1;
}

static void extract_entries(CTX)
{
	struct header* hdr;

	while((hdr = read_head(ctx)))
		if(!extract_single_entry(ctx, hdr))
			return;
}

static void setup_list_bufs(CTX, struct bufout* bo)
{
	void* outbuf;
	void* hdrbuf;

	int outlen = 2*PAGE;
	int hdrlen = 2*PAGE;
	int total = outlen + hdrlen;

	heap_init(ctx, total);

	outbuf = heap_alloc(ctx, outlen);
	hdrbuf = heap_alloc(ctx, hdrlen);

	bufoutset(bo, STDOUT, outbuf, outlen);

	ctx->head = hdrbuf;
	ctx->hlen = hdrlen;
}

void cmd_list(CTX)
{
	char* name = shift(ctx);
	struct bufout bo;

	no_more_arguments(ctx);

	open_cpio_file(ctx, name);
	setup_list_bufs(ctx, &bo);

	read_list_entries(ctx, &bo);

	bufoutflush(&bo);
}

void cmd_extract(CTX)
{
	char* name = shift(ctx);
	char* dir = shift(ctx);

	no_more_arguments(ctx);

	open_cpio_file(ctx, name);
	make_base_dir(ctx, dir);

	heap_init(ctx, 2*PAGE);

	int hdrlen = 2*PAGE;
	void* hdrbuf = heap_alloc(ctx, hdrlen);

	ctx->head = hdrbuf;
	ctx->hlen = hdrlen;

	extract_entries(ctx);
}
