#include <bits/elf.h>

#include <string.h>
#include <util.h>

#include "shead.h"

struct elf_shdr {
	uint type;
	uint offset;
	uint size;
	uint flags;
	uint link;
	uint info;
	uint entsize;
	uint align;
};

static void append_shstring(char* str)
{
	uint off = D.ptr;
	uint len = strlen(str);
	char* buf = alloc_append(len + 1);

	memcpy(buf, str, len);

	buf[len] = '\0';

	uint idx = D.shstrtab.count;

	if(idx >= NSTRINGS)
		fail("strtab overflow", NULL, 0);

	D.shstrtab.off[idx] = (off - D.shstrtab.offset);
	D.shstrtab.count = idx + 1;
}

static void append_shstrtab(void)
{
	D.shstrtab.offset = D.ptr;

	append_shstring("");
	append_shstring(".dynsym");
	append_shstring(".dynstr");

	if(D.versym.offset)
		append_shstring(".versym");
	if(D.verdef.offset)
		append_shstring(".verdef");

	append_shstring(".dynamic");
	append_shstring(".shstrtab");

	append_pad8();

	D.shstrtab.size = D.ptr - D.shstrtab.offset;
}

static void append_dyn(uint tag, uint val)
{
	if(elf64) {
		struct elf64dyn* dyn = alloc_append(sizeof(*dyn));

		F.stx(&dyn->tag, tag);
		F.stx(&dyn->val, val);
	} else {
		struct elf32dyn* dyn = alloc_append(sizeof(*dyn));

		F.stw(&dyn->tag, tag);
		F.stw(&dyn->val, val);
	}
}

static void append_dynamic(void)
{
	D.dynamic.offset = D.ptr;

	append_dyn(DT_SONAME, D.soname.offset);
	append_dyn(DT_NULL, 0);

	D.dynamic.size = D.ptr - D.dynamic.offset;
}

static uint shstr_offset(uint idx)
{
	if(idx >= D.shstrtab.count)
		return 0;

	return D.shstrtab.off[idx];
}

static void add_section_64(struct elf_shdr* sec, uint noff)
{
	struct elf64shdr* sh = alloc_append(sizeof(*sh));

	F.stw(&sh->type, sec->type);
	F.stw(&sh->name, noff);
	F.stx(&sh->offset, sec->offset);
	F.stx(&sh->size, sec->size);
	F.stw(&sh->link, sec->link);
	F.stw(&sh->info, sec->info);
	F.stx(&sh->flags, sec->flags);
	F.stx(&sh->entsize, sec->entsize);
	F.stx(&sh->align, sec->align);
}

static void add_section_32(struct elf_shdr* sec, uint noff)
{
	struct elf32shdr* sh = alloc_append(sizeof(*sh));

	F.stw(&sh->type, sec->type);
	F.stw(&sh->name, noff);
	F.stw(&sh->offset, sec->offset);
	F.stw(&sh->size, sec->size);
	F.stw(&sh->link, sec->link);
	F.stw(&sh->info, sec->info);
	F.stw(&sh->flags, sec->flags);
	F.stw(&sh->entsize, sec->entsize);
	F.stw(&sh->align, sec->align);
}

static void clr_section(struct elf_shdr* sec)
{
	memzero(sec, sizeof(*sec));
}

static void add_section(struct elf_shdr* sec)
{
	uint idx = D.sections.count;
	uint noff = shstr_offset(idx);

	if(elf64)
		add_section_64(sec, noff);
	else
		add_section_32(sec, noff);

	D.sections.count = idx + 1;
}

static void add_sec_empty(void)
{
	struct elf_shdr sec;

	clr_section(&sec);

	sec.type = SHT_NULL;

	add_section(&sec);
}

static void add_sec_dynsym(void)
{
	struct elf_shdr sec;

	uint entsize = elf64 ?
	               sizeof(struct elf64sym) :
		       sizeof(struct elf32sym);

	clr_section(&sec);

	sec.type = SHT_DYNSYM;
	sec.offset = D.dynsym.offset;
	sec.size = D.dynsym.size;
	sec.entsize = entsize;
	sec.align = 8;
	sec.link = 2;
	sec.info = 1;

	add_section(&sec);
}

static void add_sec_dynstr(void)
{
	struct elf_shdr sec;

	clr_section(&sec);

	sec.type = SHT_STRTAB;
	sec.offset = D.dynstr.offset;
	sec.size = D.dynstr.size;

	add_section(&sec);
}

static void add_sec_dynamic(void)
{
	struct elf_shdr sec;

	uint entsize = elf64 ?
	               sizeof(struct elf64dyn) :
		       sizeof(struct elf32dyn);

	clr_section(&sec);

	sec.type = SHT_DYNAMIC;
	sec.offset = D.dynamic.offset;
	sec.size = D.dynamic.size;
	sec.entsize = entsize;
	sec.align = 8;
	sec.link = 2;
	sec.flags = SHF_ALLOC;

	add_section(&sec);
}

static void add_sec_versym(void)
{
	struct elf_shdr sec;

	if(!D.versym.offset)
		return;

	clr_section(&sec);

	sec.type = SHT_GNU_versym;
	sec.offset = D.versym.offset;
	sec.size = D.versym.size;
	sec.entsize = 2;
	sec.align = 2;
	sec.link = 1;
	sec.flags = SHF_ALLOC;

	add_section(&sec);
}

static void add_sec_verdef(void)
{
	struct elf_shdr sec;

	if(!D.verdef.offset)
		return;

	clr_section(&sec);

	sec.type = SHT_GNU_verdef;
	sec.offset = D.verdef.offset;
	sec.size = D.verdef.size;
	sec.entsize = 14;
	sec.align = 8;
	sec.link = 2;
	sec.info = D.verdef.info;
	sec.flags = SHF_ALLOC;

	add_section(&sec);
}

static void add_sec_shstrtab(void)
{
	struct elf_shdr sec;

	D.shstrtab.shndx = D.sections.count;

	clr_section(&sec);

	sec.type = SHT_STRTAB;
	sec.offset = D.shstrtab.offset;
	sec.size = D.shstrtab.size;

	add_section(&sec);
}

static void append_stub_sections(void)
{
	uint i, n = D.symsec.count;
	struct elf_shdr sec;

	D.symsec.start = D.sections.count;

	for(i = 0; i < n; i++) {
		uint key = D.symsec.keys[i];
		uint type = (key & 1) ? SHT_PROGBITS : SHT_NOBITS;
		uint flags = key >> 1;

		clr_section(&sec);

		sec.type = type;
		sec.flags = flags;
		sec.align = 8;

		add_section(&sec);
	}
}

static void append_shdrs(void)
{
	D.sections.offset = D.ptr;

	add_sec_empty();

	add_sec_dynsym();
	add_sec_dynstr();

	add_sec_versym();
	add_sec_verdef();

	add_sec_dynamic();
	add_sec_shstrtab();

	append_stub_sections();
}

static void append_phdrs(void)
{
	D.segments.offset = D.ptr;

	uint offset = D.dynamic.offset;
	uint length = D.dynamic.size;

	if(elf64) {
		struct elf64phdr* ph = alloc_append(sizeof(*ph));

		F.stw(&ph->type, PT_DYNAMIC);
		F.stw(&ph->flags, PF_R);
		F.stx(&ph->offset, offset);
		F.stx(&ph->vaddr, 0x00000000);
		F.stx(&ph->paddr, 0x00000000);
		F.stx(&ph->filesz, length);
		F.stx(&ph->memsz, length);
		F.stx(&ph->align, 0);
	} else {
		struct elf32phdr* ph = alloc_append(sizeof(*ph));

		F.stw(&ph->type, PT_DYNAMIC);
		F.stw(&ph->flags, PF_R);
		F.stw(&ph->offset, offset);
		F.stw(&ph->vaddr, 0x00000000);
		F.stw(&ph->paddr, 0x00000000);
		F.stw(&ph->filesz, length);
		F.stw(&ph->memsz, length);
		F.stw(&ph->align, 0);
	}

	D.segments.count++;
}

static void update_header(void)
{
	uint shstrndx = D.shstrtab.shndx;

	if(elf64) {
		struct elf64hdr* hdr = D.buf;

		F.stx(&hdr->phoff, D.segments.offset);
		F.sts(&hdr->phnum, D.segments.count);
		F.sts(&hdr->phentsize, sizeof(struct elf64phdr));

		F.stx(&hdr->shoff, D.sections.offset);
		F.sts(&hdr->shnum, D.sections.count);
		F.sts(&hdr->shentsize, sizeof(struct elf64shdr));

		F.sts(&hdr->shstrndx, shstrndx);
	} else {
		struct elf32hdr* hdr = D.buf;

		F.stw(&hdr->phoff, D.segments.offset);
		F.sts(&hdr->phnum, D.segments.count);
		F.sts(&hdr->phentsize, sizeof(struct elf32phdr));

		F.stw(&hdr->shoff, D.sections.offset);
		F.sts(&hdr->shnum, D.sections.count);
		F.sts(&hdr->shentsize, sizeof(struct elf32shdr));

		F.sts(&hdr->shstrndx, shstrndx);
	}
}

static void copy_header(void)
{
	uint size;

	if(elf64)
		size = sizeof(struct elf64hdr);
	else
		size = sizeof(struct elf32hdr);

	void* dst = alloc_append(size);

	memcpy(dst, S.buf, size);
}

void compose_output(void)
{
	copy_header();

	transfer_symbols();

	append_dynamic();
	append_shstrtab();

	append_shdrs();
	append_phdrs();

	update_symbols();
	update_header();
}
