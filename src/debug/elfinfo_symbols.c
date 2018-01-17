#include <bits/elf.h>
#include <format.h>
#include <string.h>
#include <printf.h>
#include <util.h>

#include "elfinfo.h"

struct ypad {
	int size;
	int addr;
};

struct shdr {
	uint32_t name;
	uint32_t type;
	uint64_t offset;
	uint64_t size;
};

struct sym {
	uint32_t name;
	byte info;
	byte other;
	uint16_t shndx;
	uint64_t addr;
	uint64_t size;
};

#define SH struct shdr* sh
#define SM struct sym* sm
#define PAD struct ypad* pad

static int symbol_stride(CTX)
{
	if(ctx->elf64)
		return sizeof(struct elf64sym);
	else
		return sizeof(struct elf32sym);
}

static void load_symbol(CTX, SM, void* ptr)
{
	int elf64 = ctx->elf64;
	int elfxe = ctx->elfxe;

	copy_u32(elfsym, ptr, name, &sm->name);
	copy_u8(elfsym, ptr, info, &sm->info);
	copy_u8(elfsym, ptr, other, &sm->other);
	copy_u16(elfsym, ptr, shndx, &sm->shndx);
	copy_x64(elfsym, ptr, value, &sm->addr);
	copy_x64(elfsym, ptr, size, &sm->size);
}

static int dec_digits_in(uint x)
{
	int n = 1;

	while(x >= 10) { x /= 10; n++; }

	return n;
}

static int hex_digits_in(uint64_t x)
{
	int n = 1;

	if(!x) return 0;

	while(x >= 16) { x >>= 4; n++; }

	return n;
}

static void prep_symbol(CTX, SM, PAD)
{
	int addr = hex_digits_in(sm->addr);
	int size = dec_digits_in(sm->size);

	if(addr > pad->addr)
		pad->addr = addr;
	if(size > pad->size)
		pad->size = size;
}

static void prep_symbol_padding(CTX, SH, PAD)
{
	int elf64 = ctx->elf64;
	void* ptr = ctx->buf + sh->offset;
	void* end = ptr + sh->size;
	int stride = elf64 ? sizeof(struct elf64sym) : sizeof(struct elf32sym);
	struct sym sym;

	for(; ptr < end; ptr += stride) {
		load_symbol(ctx, &sym, ptr);
		prep_symbol(ctx, &sym, pad);
	}
}

static char* fmt_addr(char* p, char* e, SM, PAD)
{
	if(sm->shndx) {
		p = fmtstr(p, e, "0x");
		p = fmtpad0(p, e, pad->addr, fmtx64(p, e, sm->addr));
	} else {
		p = fmtstr(p, e, " -");
		p = fmtpad(p, e, pad->addr, p);
	}

	return p;
}

static char* fmt_info(char* p, char* e, SM)
{
	uint32_t info = sm->info;
	int bind = ELF_SYM_BIND(info);
	int type = ELF_SYM_TYPE(info);

	if(bind == STB_LOCAL)
		p = fmtchar(p, e, ' ');
	else if(bind == STB_WEAK)
		p = fmtchar(p, e, 'w');
	else if(bind == STB_GLOBAL)
		p = fmtchar(p, e, 'G');
	else
		p = fmtchar(p, e, '?');

	p = fmtchar(p, e, ' ');

	if(type == STT_NOTYPE && !sm->shndx)
		p = fmtstr(p, e, "UND");
	else if(type == STT_NOTYPE)
		p = fmtstr(p, e, " - ");
	else if(type == STT_OBJECT)
		p = fmtstr(p, e, "OBJ");
	else if(type == STT_FUNC)
		p = fmtstr(p, e, "FUN");
	else if(type == STT_COMMON)
		p = fmtstr(p, e, "COM");
	else
		p = fmtpad(p, e, 3, fmtbyte(p, e, type));

	return p;
}

static char* fmt_name(char* p, char* e, uint32_t off, CTX)
{
	const char* str;

	if((str = lookup_string(ctx, off))) {
		p = fmtstr(p, e, "  ");
		p = fmtstr(p, e, str);
	} else {
		p = fmtstr(p, e, "  name ");
		p = fmtu64(p, e, off);
	}

	return p;
}

static void dump_source(CTX, SM)
{
	const char* str;

	if(!(str = lookup_string(ctx, sm->name)))
		return;
	if(!*str)
		return;

	output(ctx, str, strlen(str));
	output(ctx, "\n", 1);
}

static void dump_symbol(CTX, SM, PAD)
{
	FMTBUF(p, e, buf, 100);

	p = fmt_addr(p, e, sm, pad);
	p = fmtstr(p, e, " ");
	p = fmt_info(p, e, sm);
	p = fmtstr(p, e, " ");

	if(sm->size)
		p = fmtpad(p, e, pad->size, fmtu64(p, e, sm->size));
	else
		p = fmtpad(p, e, pad->size, fmtstr(p, e, "-"));
	if(sm->name)
		p = fmt_name(p, e, sm->name, ctx);


	FMTENL(p, e);

	output(ctx, buf, p - buf);

	ctx->count++;
}

static int special_symbol_type(SM)
{
	int type = ELF_SYM_TYPE(sm->info);

	if(!sm->info && !sm->name)
		return 1; /* skip empty symbols */
	if(type == STT_FILE || type == STT_SECTION)
		return 1; /* skip sources and sections */

	return 0;
}

static void dump_source_section(CTX, SH)
{
	void* ptr = ctx->buf + sh->offset;
	void* end = ptr + sh->size;
	int stride = symbol_stride(ctx);
	struct sym sym;

	for(; ptr < end; ptr += stride) {
		load_symbol(ctx, &sym, ptr);

		if(special_symbol_type(&sym))
			continue;

		dump_source(ctx, &sym);

		ctx->count++;
	}
}

static void dump_symbol_section(CTX, SH, PAD, int ndx)
{
	void* ptr = ctx->buf + sh->offset;
	void* end = ptr + sh->size;
	int stride = symbol_stride(ctx);
	struct sym sym;

	for(; ptr < end; ptr += stride) {
		load_symbol(ctx, &sym, ptr);

		if(ndx && sym.shndx != ndx)
			continue;

		int type = ELF_SYM_TYPE(sym.info);

		if(!sym.info && !sym.name)
			continue; /* skip empty symbols */
		if(type == STT_FILE || type == STT_SECTION)
			continue; /* skip sources and sections */

		dump_symbol(ctx, &sym, pad);

		ctx->count++;
	}
}

static int load_section(CTX, SH, int i, uint type)
{
	int elf64 = ctx->elf64;
	int elfxe = ctx->elfxe;

	uint64_t shoff = ctx->shoff;
	uint16_t shentsize = ctx->shentsize;
	void* ptr = ctx->buf + shoff + i*shentsize;

	copy_u32(elfshdr, ptr, type,   &sh->type);

	if(type && sh->type != type) return -1;

	copy_u32(elfshdr, ptr, name,   &sh->name);
	copy_x64(elfshdr, ptr, offset, &sh->offset);
	copy_x64(elfshdr, ptr, size,   &sh->size);

	return 0;
}

static void locate_strtab_section(CTX)
{
	struct shdr hdr, *sh = &hdr;
	uint16_t i, shnum = ctx->shnum;
	const char* name;

	locate_strings_section(ctx);

	for(i = 0; i < shnum; i++) {
		if(load_section(ctx, sh, i, SHT_STRTAB) < 0)
			continue;
		if(!(name = lookup_string(ctx, sh->name)))
			continue;
		if(strcmp(name, ".strtab"))
			continue;

		return use_strings_at_offset(ctx, sh->offset, sh->size);
	}

	reset_strings_location(ctx);
}

void dump_symbols(CTX)
{
	struct shdr hdr, *sh = &hdr;
	uint16_t i, shnum = ctx->shnum;
	struct ypad pad;

	memzero(&pad, sizeof(pad));
	ctx->count = 0;

	locate_strtab_section(ctx);

	for(i = 0; i < shnum; i++)
		if(load_section(ctx, sh, i, SHT_SYMTAB) >= 0)
			prep_symbol_padding(ctx, sh, &pad);

	for(i = 0; i < shnum; i++)
		if(load_section(ctx, sh, i, SHT_SYMTAB) >= 0)
			dump_symbol_section(ctx, sh, &pad, 0);

	if(!ctx->count)
		fail("no symbols found", NULL, 0);
}

static void prep_symbol_marking(CTX, SH, PAD, byte* marks)
{
	void* ptr = ctx->buf + sh->offset;
	void* end = ptr + sh->size;
	int stride = symbol_stride(ctx);
	uint16_t shnum = ctx->shnum;
	struct sym sym;

	for(; ptr < end; ptr += stride) {
		load_symbol(ctx, &sym, ptr);

		if(special_symbol_type(&sym))
			continue;

		prep_symbol(ctx, &sym, pad);

		int shndx = sym.shndx;

		if(!shndx || shndx >= shnum)
			continue;

		marks[shndx] = 1;
	}
}

void dump_section_comment(CTX, int s)
{
	const char* namestr;
	struct shdr hdr, *sh = &hdr;

	if(load_section(ctx, sh, s, 0) < 0)
		namestr = NULL;
	else
		namestr = lookup_string(ctx, sh->name);

	if(namestr) {
		outstr(ctx, "# Section ");
		outstr(ctx, namestr);
	} else {
		outstr(ctx, "# Section ");

		FMTBUF(p, e, buf, 20);
		p = fmtint(p, e, s);
		FMTEND(p, e);

		output(ctx, buf, p - buf);

		outstr(ctx, " (invalid name)");
	}

	outstr(ctx, "\n");
}

void dump_sect_syms(CTX)
{
	struct shdr hdr, *sh = &hdr;
	uint16_t i, s, shnum = ctx->shnum;
	struct ypad pad;
	byte marks[shnum];

	memzero(&pad, sizeof(pad));
	memzero(marks, sizeof(marks));

	ctx->count = 0;
	ctx->sectmarks = marks;

	for(i = 0; i < shnum; i++)
		if(load_section(ctx, sh, i, SHT_SYMTAB) >= 0)
			prep_symbol_marking(ctx, sh, &pad, marks);

	for(s = 0; s < shnum; s++) {
		if(!marks[s])
			continue;

		locate_strings_section(ctx);

		dump_section_comment(ctx, s);

		locate_strtab_section(ctx);

		for(i = 0; i < shnum; i++)
			if(load_section(ctx, sh, i, SHT_SYMTAB) >= 0)
				dump_symbol_section(ctx, sh, &pad, s);
	}

	if(!ctx->count)
		fail("no symbols found", NULL, 0);
}

void dump_sources(CTX)
{
	struct shdr hdr, *sh = &hdr;
	uint16_t i, shnum = ctx->shnum;

	ctx->count = 0;

	locate_strtab_section(ctx);

	for(i = 0; i < shnum; i++)
		if(load_section(ctx, sh, i, SHT_SYMTAB) >= 0)
			dump_source_section(ctx, sh);

	if(!ctx->count)
		fail("no source symbols found", NULL, 0);
}
