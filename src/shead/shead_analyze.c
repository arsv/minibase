#include <bits/elf.h>

#include <endian.h>
#include <string.h>
#include <util.h>

#include "shead.h"

#ifdef BIGENDIAN
#define NATIVE ELF_MSB
#else
#define NATIVE ELF_LSB
#endif

uint lds_ne(uint16_t* addr) { return *addr; }
uint ldw_ne(uint32_t* addr) { return *addr; }
uint ldx_ne(uint64_t* addr) { return *addr; }

uint lds_xe(uint16_t* addr) { return swabs(*addr); }
uint ldw_xe(uint32_t* addr) { return swabl(*addr); }
uint ldx_xe(uint64_t* addr) { return swabx(*addr); }

void sts_ne(uint16_t* addr, uint val) { *addr = val; }
void stw_ne(uint32_t* addr, uint val) { *addr = val; }
void stx_ne(uint64_t* addr, uint val) { *addr = val; }

void sts_xe(uint16_t* addr, uint val) { *addr = swabs(val); }
void stw_xe(uint32_t* addr, uint val) { *addr = swabl(val); }
void stx_xe(uint64_t* addr, uint val) { *addr = swabx(val); }

static struct elf32shdr* get_shent_32(uint i)
{
	if(i > S.header.shnum)
		fail("invalid section index", NULL, 0);

	struct elf32shdr* sh = srcptr(S.header.shoff);

	return &sh[i];
}

static struct elf64shdr* get_shent_64(uint i)
{
	if(i > S.header.shnum)
		fail("invalid section index", NULL, 0);

	struct elf64shdr* sh = srcptr(S.header.shoff);

	return &sh[i];
}

void setup_native_endian(void)
{
	F.lds = lds_ne;
	F.ldw = ldw_ne;
	F.ldx = ldx_ne;

	F.sts = sts_ne;
	F.stw = stw_ne;
	F.stx = stx_ne;
}

void setup_cross_endian(void)
{
	F.lds = lds_xe;
	F.ldw = ldw_xe;
	F.ldx = ldx_xe;

	F.sts = sts_xe;
	F.stw = stw_xe;
	F.stx = stx_xe;
}

static void read_elf_ident(void)
{
	struct elfhdr* hdr = S.buf;

	if(S.len < sizeof(*hdr))
		fail("truncated header", NULL, 0);
	if(memcmp(hdr->tag, ELFTAG, 4))
		fail("not an ELF header", NULL, 0);

	byte bit = hdr->class;
	byte end = hdr->data;

	if(bit == ELF_64)
		elf64 = 1;
	else if(bit != ELF_32)
		fail("unexpected ELF hdr class", NULL, bit);

	if((end != ELF_LSB) && (end != ELF_MSB))
		fail("unexpected ELF hdr data", NULL, end);

	if(end == NATIVE)
		setup_native_endian();
	else
		setup_cross_endian();
}

int check_fit(void* ptr, uint size)
{
	void* buf = S.buf;
	void* end = buf + S.len;

	if(ptr < buf)
		fail("invalid ptr check", NULL, 0);

	return (ptr + size > end);
}

static void read_header_64(void)
{
	struct elf64hdr* hdr = S.buf;

	if(check_fit(hdr, sizeof(*hdr)))
		fail("truncated header", NULL, 0);

	uint phentsize = F.lds(&hdr->phentsize);
	uint shentsize = F.lds(&hdr->shentsize);

	if(phentsize != sizeof(struct elf64phdr))
		fail("unexpected phentsize", NULL, 0);
	if(shentsize != sizeof(struct elf64shdr))
		fail("unexpected shentsize", NULL, 0);

	S.header.phoff = F.ldx(&hdr->phoff);
	S.header.phnum = F.lds(&hdr->phnum);
	S.header.shoff = F.ldx(&hdr->shoff);
	S.header.shnum = F.lds(&hdr->shnum);
	S.header.shstrndx = F.lds(&hdr->shstrndx);
}

static void read_header_32(void)
{
	struct elf32hdr* hdr = S.buf;

	if(check_fit(hdr, sizeof(*hdr)))
		fail("truncated header", NULL, 0);

	uint phentsize = F.lds(&hdr->phentsize);
	uint shentsize = F.lds(&hdr->shentsize);

	if(phentsize != sizeof(struct elf32phdr))
		fail("unexpected phentsize", NULL, 0);
	if(shentsize != sizeof(struct elf32shdr))
		fail("unexpected shentsize", NULL, 0);

	S.header.phoff = F.ldw(&hdr->phoff);
	S.header.phnum = F.lds(&hdr->phnum);
	S.header.shoff = F.ldw(&hdr->shoff);
	S.header.shnum = F.lds(&hdr->shnum);
	S.header.shstrndx = F.lds(&hdr->shstrndx);
}

static void check_dynsym_64(struct elf64shdr* sh)
{
	uint entsize = F.ldx(&sh->entsize);

	if(entsize != sizeof(struct elf64sym))
		fail("invalid dynstr entsize", NULL, 0);
	if(S.dynsym.offset)
		fail("multiple dynsym sections", NULL, 0);

	S.dynsym.info = F.ldw(&sh->info);
	S.dynsym.offset = F.ldx(&sh->offset);
	S.dynsym.size = F.ldx(&sh->size);

	uint link = F.ldw(&sh->link);

	struct elf64shdr* ls = get_shent_64(link);

	uint type = F.ldw(&ls->type);

	if(type != SHT_STRTAB)
		fail("non-STRTAB dynstr", NULL, 0);

	S.dynsym.stroff = F.ldx(&ls->offset);
	S.dynsym.strlen = F.ldx(&ls->size);
}

static void check_versym_64(struct elf64shdr* sh)
{
	S.versym.offset = F.ldx(&sh->offset);
	S.versym.size = F.ldx(&sh->size);
}

static void check_versym_32(struct elf32shdr* sh)
{
	S.versym.offset = F.ldw(&sh->offset);
	S.versym.size = F.ldw(&sh->size);
}

static void check_verdef_64(struct elf64shdr* sh)
{
	S.verdef.offset = F.ldx(&sh->offset);
	S.verdef.size = F.ldx(&sh->size);
	S.verdef.info = F.ldw(&sh->info);

	uint link = F.ldw(&sh->link);
	struct elf64shdr* ls = get_shent_64(link);

	uint type = F.ldw(&ls->type);

	if(type != SHT_STRTAB)
		fail("non-STRTAB verdef link", NULL, 0);

	S.verdef.stroff = F.ldx(&ls->offset);
	S.verdef.strlen = F.ldx(&ls->size);
}

static void check_verdef_32(struct elf32shdr* sh)
{
	S.verdef.offset = F.ldw(&sh->offset);
	S.verdef.size = F.ldw(&sh->size);
	S.verdef.info = F.ldw(&sh->info);

	uint link = F.ldw(&sh->link);
	struct elf32shdr* ls = get_shent_32(link);

	uint type = F.ldw(&ls->type);

	if(type != SHT_STRTAB)
		fail("non-STRTAB verdef link", NULL, 0);

	S.verdef.stroff = F.ldw(&ls->offset);
	S.verdef.strlen = F.ldw(&ls->size);
}

static void scan_sections_64(void)
{
	struct elf64shdr* shtab = srcptr(S.header.shoff);
	uint i, n = S.header.shnum;

	for(i = 0; i < n; i++) {
		struct elf64shdr* sh = &shtab[i];
		uint type = F.ldw(&sh->type);

		if(type == SHT_DYNSYM)
			check_dynsym_64(sh);
		if(type == SHT_GNU_versym)
			check_versym_64(sh);
		if(type == SHT_GNU_verdef)
			check_verdef_64(sh);
	}
}

static void check_dynsym_32(struct elf32shdr* sh)
{
	uint entsize = sh->entsize;

	if(entsize != sizeof(struct elf32sym))
		fail("invalid dynstr entsize", NULL, 0);
	if(S.dynsym.offset)
		fail("multiple dynsym sections", NULL, 0);

	S.dynsym.info = F.ldw(&sh->info);
	S.dynsym.offset = F.ldw(&sh->offset);
	S.dynsym.size = F.ldw(&sh->size);

	uint link = F.ldw(&sh->link);

	struct elf32shdr* ls = get_shent_32(link);

	uint type = F.ldw(&ls->type);

	if(type != SHT_STRTAB)
		fail("non-STRTAB .dynstr", NULL, 0);

	S.dynsym.stroff = F.ldw(&ls->offset);
	S.dynsym.strlen = F.ldw(&ls->size);
}

static void scan_sections_32(void)
{
	struct elf32shdr* shtab = srcptr(S.header.shoff);
	uint i, n = S.header.shnum;

	for(i = 0; i < n; i++) {
		struct elf32shdr* sh = &shtab[i];
		uint type = F.ldw(&sh->type);

		if(type == SHT_DYNSYM)
			check_dynsym_32(sh);
		if(type == SHT_GNU_versym)
			check_versym_32(sh);
		if(type == SHT_GNU_verdef)
			check_verdef_32(sh);
	}
}

static void assess_sections(void)
{
	if(!S.dynsym.offset)
		fail("no dynsyms", NULL, 0);
}

void analyze_source(void)
{
	read_elf_ident();

	if(elf64) {
		read_header_64();
		scan_sections_64();
	} else {
		read_header_32();
		scan_sections_32();
	}

	assess_sections();
}
