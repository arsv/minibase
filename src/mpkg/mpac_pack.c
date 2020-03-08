#include <sys/file.h>
#include <sys/mman.h>
#include <sys/fpath.h>
#include <sys/dents.h>
#include <sys/splice.h>

#include <string.h>
#include <format.h>
#include <main.h>
#include <util.h>

#include "mpac.h"

/* Custom-structure packing code. Read a file that describes which data
   to put into the archive, and which directory structure to build there.

   The intended use for this is picking *some* files from the filesystem,
   and stuffing them into a tree that does not necessary match the local
   filesystem. Think of a build directory for a package, where we need to
   pick executables and manual pages only, and put them under bin/ and man/
   respectively in the resulting package.

   With some effort, this should allow building packages the need to have
   or run explicit `make install`. Just build the source, and collect files
   from their build-time locations. */

#define MAX_LIST_SIZE (1<<24)

#define ENT_DIR  (1<<24)
#define ENT_LINK (1<<25)
#define ENT_EXEC (1<<26)
#define ENT_NLEN ((1<<12) - 1)

struct ent {
	uint size;
	uint bits;
	char* name;
};

struct listctx {
	struct top* ctx;

	int fd;
	char* name;
	void* buf;
	int len;
	int line;

	struct ent* index;
	int count;

	void* head;
	uint hlen;

	int outfd;
	char* output;

	struct ent* ldir;
	struct ent* last;
};

#define LCT struct listctx* lct

static void open_file_list(LCT, char* name)
{
	int fd, ret;
	struct stat st;
	void* buf;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", name, ret);

	if(st.size > MAX_LIST_SIZE)
		fail(NULL, name, -E2BIG);

	int size = st.size;
	int proto = PROT_READ;
	int flags = MAP_PRIVATE;

	buf = sys_mmap(NULL, size, proto, flags, fd, 0);

	if((ret = mmap_error(buf)))
		fail("mmap", name, ret);

	lct->fd = fd;
	lct->name = name;
	lct->buf = buf;
	lct->len = size;
}

static void syntax(LCT, char* msg)
{
	FMTBUF(p, e, buf, 200);
	p = fmtstr(p, e, errtag);
	p = fmtstr(p, e, ":");
	p = fmtstr(p, e, " ");
	p = fmtstr(p, e, lct->name);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, lct->line);
	p = fmtstr(p, e, ":");
	p = fmtstr(p, e, " ");
	p = fmtstr(p, e, msg);
	FMTENL(p, e);

	writeall(STDERR, buf, p - buf);
	_exit(0xFF);
}

static int basename_offset(char* name, int nlen)
{
	int i, off = 0;

	for(i = 0; i < nlen; i++)
		if(name[i] == '/')
			off = i + 1;

	return off;
}

static struct ent* add_entry(LCT, char* p, char* e, int bits)
{
	struct top* ctx = lct->ctx;
	struct ent* en = heap_alloc(ctx, sizeof(*en));

	char* name = p;
	int nlen = e - p;

	if(nlen < 0 || nlen > ENT_NLEN)
		syntax(lct, "invalid name");

	en->bits = nlen | bits;
	en->name = name;

	return en;
}

/* The ordering of the entries is checked while parsing the input file.
   It does not really matter much for the index-building code, that would
   work just fine, but the resulting index will not be accepted by mpkg.

   Also, doing it at the parsing stage makes reporting lines easier. */

static int compare(char* a, int al, char* b, int bl)
{
	int pref = al < bl ? al : bl;
	int ret;

	if((ret = memcmp(a, b, pref)))
		return ret;

	if(al < bl)
		return -1;
	if(al > bl)
		return 1;

	return 0;
}

/* For files, we need to take basenames, en->name at this point is
   still the full pathname of the source. */

static void compare_file(LCT, struct ent* en)
{
	struct ent* le = lct->last;

	if(!le) goto out;

	int llen = le->bits & ENT_NLEN;
	int nlen = en->bits & ENT_NLEN;
	int off;

	char* last = le->name;
	char* name = en->name;

	off = basename_offset(name, nlen);
	name += off; nlen -= off;

	off = basename_offset(last, llen);
	last += off; llen -= off;

	if(compare(last, llen, name, nlen) >= 0)
		syntax(lct, "file name out of order");
out:
	lct->last = en;
}

/* Directories are compared component-by-component */

static void compare_dir(LCT, struct ent* en)
{
	struct ent* el = lct->ldir;
	int ret;

	if(!el) goto out;

	int ll = el->bits & ENT_NLEN;
	char* lp = el->name;
	char* le = lp + ll;
	int nl = en->bits & ENT_NLEN;
	char* np = en->name;
	char* ne = np + nl;

	while(lp < le && np < ne) {
		char* ls = strecbrk(lp, le, '/');
		char* ns = strecbrk(np, ne, '/');

		ret = compare(lp, ls - lp, np, ns - np);

		if(ret > 0)
			goto err;
		if(ret < 0)
			goto out;

		lp = ls + 1;
		np = ns + 1;
	} if(lp < le)
		goto out;
err:
	syntax(lct, "directory out of order");
out:
	lct->ldir = en;
	lct->last = NULL;
}

static void parse_path(LCT, char* p, char* e)
{
	struct ent* en;

	for(p++; p < e; p++)
		if(*p != ' ')
			break;
	if(p >= e)
		syntax(lct, "empty path");
	if(*p == '/')
		syntax(lct, "leading slash");

	en = add_entry(lct, p, e, ENT_DIR);

	compare_dir(lct, en);
}

static void parse_file(LCT, char* p, char* e)
{
	struct ent* en;

	en = add_entry(lct, p, e, 0);

	compare_file(lct, en);
}

/* In the "pack" mode, we build two indexes: the first one is a bunch of
   `struct ent`-s referencing the lines in the list file, and the second
   one is the actual PAC index to be written to the output.

   This is a bit more complex than the "create" mode because we have to
   deal with full pathnames which do not make it into the final index.
   On the plus side, we don't have to sort them, only check the order.

   This routine builds the first (internal) index, while build_index below
   does the final PAC index. */

static void parse_input(LCT)
{
	char* p = lct->buf;
	char* e = p + lct->len;

	struct top* ctx = lct->ctx;
	struct ent* p0 = ctx->ptr;

	while(p < e) {
		char* s = p;
		char* q = strecbrk(p, e, '\n');

		p = q + 1;

		lct->line++;

		if(s >= q)
			continue;

		char lead = *s;

		if(lead == '#')
			continue;
		if(lead == '>')
			parse_path(lct, s, q);
		else
			parse_file(lct, s, q);
	}

	struct ent* p1 = ctx->ptr;

	lct->index = p0;
	lct->count = p1 - p0;
}

static char* put_dir(LCT, int lvl, char* name, int nlen)
{
	void* bp = heap_alloc(lct->ctx, 1 + nlen + 1);

	byte* lead = bp++;
	char* nbuf = bp;

	*lead = TAG_DIR | lvl;

	memcpy(nbuf, name, nlen);
	nbuf[nlen] = '\0';

	return nbuf;
}

/* New path directive ("> foo/bar") in the config file means we need
   to append directory tags to the index. This is done in two steps:
   first, we figure out common prefix wrt what is stored in ctx->path,
   and skip it; then, we append dir tags for whatever follows the common
   prefix only. So if ctx->path is [ "foo", "blah" ] and we have "foo/bar"
   incoming, we skip depth 0 "foo" and append a depth 1 entry for "bar". */

static void index_path(LCT, struct ent* en)
{
	struct top* ctx = lct->ctx;
	char** path = ctx->path;
	int depth = ctx->depth;

	int nlen = en->bits & ENT_NLEN;
	char* p = en->name;
	char* e = p + nlen;
	int lvl = 0;

	while(p < e) {
		char* q = strecbrk(p, e, '/');
		char* comp = p; /* path component */
		int clen = q - p; /* and its length */

		if(lvl >= depth)
			break;
		if(strncmp(path[lvl], comp, clen))
			break;

		p = q + 1; lvl++;
	}

	while(p < e) {
		char* q = strecbrk(p, e, '/');
		char* comp = p;
		int clen = q - p;

		char* name = put_dir(lct, lvl, comp, clen);

		path[lvl++] = name;

		p = q + 1;
	}

	ctx->depth = lvl;
}

static void stat_entry(struct ent* en, struct stat* st)
{
	int bits = en->bits;
	int len = bits & ENT_NLEN;
	char* tmp = alloca(len + 1);

	memcpy(tmp, en->name, len);
	tmp[len] = '\0';

	int at = AT_FDCWD;
	int flags = AT_SYMLINK_NOFOLLOW;
	int ret;

	if((ret = sys_fstatat(at, tmp, st, flags)) < 0)
		fail(NULL, tmp, ret);

	int mode = st->mode;
	int type = mode & S_IFMT;

	if(st->size > 0xFFFFFFFFU)
		fail(NULL, tmp, -E2BIG);

	uint size = st->size;

	if((type == S_IFLNK) && (size > PAGE))
		fail(NULL, tmp, -E2BIG);

	en->size = size;

	if(type == S_IFLNK)
		bits |= ENT_LINK;
	else if(type != S_IFREG)
		fail("special file:", tmp, 0);
	else if(mode & 0111)
		bits |= ENT_EXEC;

	en->bits = bits;
}

static void put_entry(LCT, struct ent* en)
{
	char* name = en->name;
	int bits = en->bits;
	int nlen = bits & ENT_NLEN;
	uint size = en->size;

	int boff = basename_offset(name, nlen);
	char* base = name + boff;
	int blen = nlen - boff;

	int nszb = 0;

	if(size > 0xFF)
		nszb++;
	if(size > 0xFFFF)
		nszb++;
	if(size > 0xFFFFFF)
		nszb++;

	int lead;

	if(bits & ENT_EXEC)
		lead = TAG_EXEC;
	else if(bits & ENT_LINK)
		lead = TAG_LINK;
	else
		lead = TAG_FILE;

	int need = 1 + 1 + nszb + blen + 1;

	byte* bp = heap_alloc(lct->ctx, need);
	byte* be = bp + need;

	*bp++ = lead | nszb;

	for(int i = 0; i <= nszb; i++) {
		*bp++ = (size & 0xFF);
		size = size >> 8;
	}

	memcpy(bp, base, blen);
	bp += blen;
	*bp++ = '\0';

	if(bp != be) fail("index build failure", NULL, 0);
}

static void index_file(LCT, struct ent* en)
{
	struct stat st;

	stat_entry(en, &st);

	put_entry(lct, en);
}

/* Build the final PAC index using the data from the internal index.

   We allocate 8 bytes at the start to put PACx[size] header later. */

static void build_index(LCT)
{
	int i, n = lct->count;
	struct ent* index = lct->index;

	lct->head = heap_alloc(lct->ctx, 8);

	void* p0 = lct->ctx->ptr;

	for(i = 0; i < n; i++) {
		struct ent* en = &index[i];

		if(en->bits & ENT_DIR)
			index_path(lct, en);
		else
			index_file(lct, en);
	}

	void* p1 = lct->ctx->ptr;

	if(p1 <= p0)
		fail("refusing to make an empty archive", NULL, 0);

	uint len = p1 - p0;

	if(p0 + len != p1)
		fail("excessive index size", NULL, 0);

	lct->hlen = len;
}

static void write_index(LCT)
{
	int fd = lct->outfd;
	uint len = lct->hlen;
	int nszb = 0;

	if(len > 0xFF)
		nszb++;
	if(len > 0xFFFF)
		nszb++;
	if(len > 0xFFFFFF)
		nszb++;

	void* head = lct->head + 3 - nszb;
	byte* tag = head;

	*tag++ = 'P';
	*tag++ = 'A';
	*tag++ = 'C';
	*tag++ = '@' | nszb;

	uint size = len;

	for(int i = 0; i <= nszb; i++) {
		*tag++ = size & 0xFF;
		size = size >> 8;
	}

	long total = len + 5 + nszb;
	long ret;

	if((ret = sys_write(fd, head, total)) < 0)
		fail("write", lct->output, ret);
	if(ret != total)
		fail("incomplete write", NULL, 0);
}

static void append_link(LCT, struct ent* en)
{
	int bits = en->bits;
	int len = bits & ENT_NLEN;
	char* tmp = alloca(len + 1);

	memcpy(tmp, en->name, len);
	tmp[len] = '\0';

	int size = en->size;
	void* buf = alloca(size);
	int ret;

	if((ret = sys_readlink(tmp, buf, size)) < 0)
		fail(NULL, tmp, ret);
	if(ret != size)
		fail("size mismatch in", tmp, 0);

	int outfd = lct->outfd;

	if((ret = sys_write(outfd, buf, size)) < 0)
		fail("write", lct->output, ret);
	if(ret != size)
		fail("incomplete write", NULL, 0);
}

static void append_file(LCT, struct ent* en)
{
	int bits = en->bits;
	int len = bits & ENT_NLEN;
	char* tmp = alloca(len + 1);

	memcpy(tmp, en->name, len);
	tmp[len] = '\0';

	int size = en->size;
	int fd;

	if((fd = sys_open(tmp, O_RDONLY)) < 0)
		fail(NULL, tmp, fd);

	int outfd = lct->outfd;
	int ret;

	if((ret = sys_sendfile(outfd, fd, NULL, size)) < 0)
		fail("sendfile", lct->output, ret);
	if(ret != size)
		fail("incomplete write", NULL, 0);
}

/* Travers the internal index once again and append the content of the files
   to the output. The works pretty much just like the "create" mode. */

static void write_content(LCT)
{
	int i, n = lct->count;
	struct ent* index = lct->index;

	for(i = 0; i < n; i++) {
		struct ent* en = &index[i];
		int bits = en->bits;

		if(bits & ENT_DIR)
			continue;
		if(bits & ENT_LINK)
			append_link(lct, en);
		else
			append_file(lct, en);
	}
}

static void open_output(LCT, char* name)
{
	int fd;
	int flags = O_WRONLY | O_CREAT | O_TRUNC;
	int mode = 0644;

	if((fd = sys_open3(name, flags, mode)) < 0)
		fail(NULL, name, fd);

	lct->outfd = fd;
	lct->output = name;
}

void cmd_pack(CTX)
{
	struct listctx context, *lct = &context;

	memzero(lct, sizeof(*lct));

	lct->ctx = ctx;

	char* outfile = shift(ctx);
	char* infile = shift(ctx);

	no_more_arguments(ctx);

	check_pac_ext(outfile);
	check_list_ext(infile);

	open_file_list(lct, infile);

	heap_init(ctx, 4*PAGE);

	parse_input(lct);
	build_index(lct);

	open_output(lct, outfile);

	write_index(lct);
	write_content(lct);
}
