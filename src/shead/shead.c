#include <bits/elf.h>

#include <sys/file.h>
#include <sys/mman.h>

#include <main.h>
#include <string.h>
#include <util.h>

#define NSTRINGS 10

ERRTAG("shead");

void* srcbuf;
uint srclen;

void* dstbuf;
uint dstlen;
uint dstptr;

struct secref {
	uint offset;
	uint length;
	uint esize;
	uint newoff;
	uint info;
} dynsym, dynstr;

struct {
	uint newoff;
	uint length;

	uint stroff[NSTRINGS];
	uint count;
} strtab;

struct {
	uint newoff;
	uint length;
} dynamic;

struct {
	uint newoff;
	uint count;
} sections, segments;

static void map_source(char* name)
{
	int fd, ret;
	struct stat st;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail("open", name, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", name, ret);
	if(st.size > 0xFFFFFFFF)
		fail("file too large", NULL, 0);

	uint size = st.size;
	int prot = PROT_READ;
	int flags = MAP_PRIVATE;

	void* buf = sys_mmap(NULL, size, prot, flags, fd, 0);

	if((ret = mmap_error(buf)))
		fail("mmap", NULL, ret);

	if((ret = sys_close(fd)) < 0)
		fail("close", name, ret);

	srclen = size;
	srcbuf = buf;
}

static uint estimate_output_size(void)
{
	return srclen;
}

static void map_output(void)
{
	uint size = estimate_output_size();
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;

	int ret;
	void* buf = sys_mmap(NULL, size, prot, flags, -1, 0);

	if((ret = mmap_error(buf)))
		fail("mmap", NULL, ret);

	dstlen = size;
	dstbuf = buf;
	dstptr = 0;
}

static void* alloc_append(uint size)
{
	uint new = dstptr + size;
	void* ptr = dstbuf + dstptr;

	if(new >= dstlen)
		fail("out of output space", NULL, 0);

	dstptr = new;

	return ptr;
}

static void write_output(char* name)
{
	int fd, ret;
	int flags = O_WRONLY | O_TRUNC | O_CREAT;
	int mode = 0755;

	if((fd = sys_open3(name, flags, mode)) < 0)
		fail("creat", name, fd);

	if((ret = sys_write(fd, dstbuf, dstptr)) < 0)
		fail("write", name, ret);

	if((ret = sys_close(fd)) < 0)
		fail("close", name, ret);
}

static void append_exact(uint offset, uint size)
{
	void* src = srcbuf + offset;

	if(size & 3)
		fail("non-aligned alloc", NULL, 0);

	void* dst = alloc_append(size);

	memcpy(dst, src, size);
}

static struct elf64shdr* locate_dynsym(void)
{
	struct elf64hdr* hdr = srcbuf;

	void* shptr = srcbuf + hdr->shoff;
	uint esize = hdr->shentsize;
	uint count = hdr->shnum;

	for(uint i = 0; i < count; i++) {
		struct elf64shdr* sh = shptr;
		shptr += esize;

		if(sh->type == SHT_DYNSYM)
			return sh;
	}

	fail("missing DYNSYM", NULL, 0);
}

static struct elf64shdr* get_dynstr(struct elf64shdr* dynsym)
{
	struct elf64hdr* hdr = srcbuf;

	uint linked = dynsym->link;

	void* shptr = srcbuf + hdr->shoff;
	uint esize = hdr->shentsize;
	uint count = hdr->shnum;

	if(!linked || linked >= count)
		fail("invalid link idx for DYNSTR", NULL, 0);

	struct elf64shdr* sh = shptr + esize*linked;

	if(sh->type != SHT_STRTAB)
		fail("invalid DYNSTR section type", NULL, 0);

	return sh;
}

static void note_section(struct secref* sr, struct elf64shdr* sh)
{
	sr->offset = sh->offset;
	sr->length = sh->size;
	sr->esize = sh->entsize;
	sr->info = sh->info;
}

static void locate_sections(void)
{
	struct elf64shdr* ds = locate_dynsym();
	struct elf64shdr* dt = get_dynstr(ds);

	note_section(&dynsym, ds);
	note_section(&dynstr, dt);
}

static void check_section(struct elf64sym* sym)
{
	uint si = sym->shndx;

	struct elf64hdr* hdr = srcbuf;
	struct elf64shdr* shs = srcbuf + hdr->shoff;
	struct elf64shdr* sh = shs + si;

	uint flags = sh->flags;

	if(flags & SHF_EXECINSTR)
		sym->shndx = 7;
	else
		sym->shndx = 8;
}

static void copy_section(struct secref* sr)
{
	sr->newoff = dstptr;
	append_exact(sr->offset, sr->length);
}

static void append_strtab(char* str)
{
	uint off = dstptr;
	uint len = strlen(str);
	char* buf = alloc_append(len + 1);

	memcpy(buf, str, len);

	buf[len] = '\0';

	uint idx = strtab.count;

	if(idx >= NSTRINGS)
		fail("strtab overflow", NULL, 0);

	strtab.stroff[idx] = (off - strtab.newoff);
	strtab.count = idx + 1;
}

static void append_shstrtab(void)
{
	uint oldptr = dstptr;

	strtab.newoff = oldptr;

	append_strtab("");
	append_strtab(".dynsym");
	append_strtab(".dynstr");
	append_strtab(".symtab");
	append_strtab(".strtab");
	append_strtab(".dynamic");
	append_strtab(".shstrtab");
	append_strtab(".text");
	append_strtab(".data");

	dstptr = (dstptr + 3) & ~3;

	strtab.length = dstptr - oldptr;
}

static void append_dynkey(uint tag, uint val)
{
	struct elf64dyn* dyn = alloc_append(sizeof(*dyn));

	dyn->tag = tag;
	dyn->val = val;
}

static struct elf64shdr* locate_dynamic()
{
	struct elf64hdr* hdr = srcbuf;

	uint i, n = hdr->shnum;
	uint stride = hdr->shentsize;
	void* ptr = srcbuf + hdr->shoff;

	for(i = 0; i < n; i++) {
		struct elf64shdr* sh = ptr;

		ptr += stride;

		if(sh->type != SHT_DYNAMIC)
			continue;

		return sh;
	}

	fail("no DYNAMIC section found", NULL, 0);
}

static void append_dynamic(void)
{
	uint oldptr = dstptr;

	struct elf64shdr* sh = locate_dynamic();

	void* ptr = srcbuf + sh->offset;
	void* end = ptr + sh->size;
	uint stride = sh->entsize;

	while(ptr < end) {
		struct elf64dyn* dyn = ptr;
		ptr += stride;

		if(dyn->tag == DT_SONAME)
			append_dynkey(DT_SONAME, dyn->val);
	}

	//append_dynkey(DT_SYMTAB, dynsym.newoff);
	//append_dynkey(DT_STRTAB, dynstr.newoff);
	//append_dynkey(DT_STRSZ, dynstr.length);

	dynamic.newoff = oldptr;
	dynamic.length = dstptr - oldptr;
}

static struct elf64shdr* sec_common(uint i, uint type)
{
	struct elf64shdr* sh = dstbuf + sections.newoff + i*sizeof(*sh);

	sh->name = strtab.stroff[i];
	sh->type = type;
	sh->align = 0;

	return sh;
}

static void sec_empty(uint i, uint type)
{
	struct elf64shdr* sh = sec_common(i, type);

	sh->size = 0;
}

static void sec_fromref(uint i, uint type, struct secref* sr, uint link)
{
	struct elf64shdr* sh = sec_common(i, type);

	sh->offset = sr->newoff;
	sh->size = sr->length;
	sh->entsize = sr->esize;
	sh->link = link;
	sh->info = sr->info;
}

static void sec_dynamic(uint i, uint type, uint link)
{
	struct elf64shdr* sh = sec_common(i, type);

	sh->offset = dynamic.newoff;
	sh->size = dynamic.length;
	sh->entsize = sizeof(struct elf64dyn);
	sh->align = 8;
	sh->flags = SHF_ALLOC;
	sh->link = link;
	sh->addr = 0x00000000;
}

static void sec_strtab(uint i, uint type)
{
	struct elf64shdr* sh = sec_common(i, type);

	sh->offset = strtab.newoff;
	sh->size = strtab.length;
	sh->entsize = 0;
	sh->align = 1;
}

static void sec_codestub(uint i, uint type)
{
	struct elf64shdr* sh = sec_common(i, type);

	sh->offset = 0;
	sh->size = 0;
	sh->entsize = 0;
	sh->align = 8;
	sh->flags = SHF_ALLOC | SHF_EXECINSTR;
}

static void sec_datastub(uint i, uint type)
{
	struct elf64shdr* sh = sec_common(i, type);

	sh->offset = 0;
	sh->size = 0;
	sh->entsize = 0;
	sh->align = 8;
	sh->flags = SHF_ALLOC | SHF_WRITE;
}

static void append_shdrs(void)
{
	uint count = 9;

	sections.count = count;
	sections.newoff = dstptr;

	struct elf64shdr* sh = alloc_append(count*sizeof(*sh));

	(void)sh;

	sec_empty(0, SHT_NULL);

	sec_fromref(1, SHT_DYNSYM, &dynsym, 2);
	sec_fromref(2, SHT_STRTAB, &dynstr, 0);

	sec_fromref(3, SHT_SYMTAB, &dynsym, 4);
	sec_fromref(4, SHT_STRTAB, &dynstr, 0);

	sec_dynamic(5, SHT_DYNAMIC, 2);
	sec_strtab(6, SHT_STRTAB);

	sec_codestub(7, SHT_PROGBITS);
	sec_datastub(8, SHT_PROGBITS);
}

static void append_phdrs(void)
{
	segments.count = 1;
	segments.newoff = dstptr;

	struct elf64phdr* ph = alloc_append(sizeof(*ph));

	uint offset = dynsym.newoff;
	uint length = dynsym.length + dynstr.length + dynamic.length + 2;

	ph->type = PT_DYNAMIC;
	ph->flags = PF_R;
	ph->offset = offset;
	ph->vaddr = 0x00000000;
	ph->paddr = 0x00000000;
	ph->filesz = length;
	ph->memsz = length;
	ph->align = 8;
}

static void update_header(void)
{
	struct elf64hdr* hdr = dstbuf;

	hdr->phoff = segments.newoff;
	hdr->phnum = segments.count;
	hdr->phentsize = sizeof(struct elf64phdr);

	hdr->shoff = sections.newoff;
	hdr->shnum = sections.count;
	hdr->shentsize = sizeof(struct elf64shdr);

	hdr->shstrndx = 6;
}

static void update_symbols(void)
{
	void* ptr = dstbuf + dynsym.newoff;
	void* end = ptr + dynsym.length;

	while(ptr < end) {
		struct elf64sym* sym = ptr;

		ptr += sizeof(*sym);

		sym->value = 0;

		if(!sym->shndx)
			continue;

		check_section(sym);
	}
}

static void compose_output(void)
{
	append_exact(0, sizeof(struct elf64hdr));

	copy_section(&dynsym);
	copy_section(&dynstr);

	append_dynamic();
	append_shstrtab();

	append_shdrs();
	append_phdrs();

	update_symbols();

	update_header();
}

int main(int argc, char** argv)
{
	if(argc != 3)
		fail("bad call", NULL, 0);

	map_source(argv[2]);
	locate_sections();

	map_output();
	compose_output();

	write_output(argv[1]);

	return 0;
}
