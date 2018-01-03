#include <bits/elf.h>
#include <format.h>
#include <printf.h>
#include <util.h>
#include "elfinfo.h"

static const char types[][4] = {
	 [0] = "NULL",
	 [1] = "BITS",
	 [2] = "SYMT",
	 [3] = "STRT",
	 [4] = "RELA",
	 [5] = "HASH",
	 [6] = "DYN ",
	 [7] = "NOTE",
	 [8] = "NONE",
	 [9] = "REL ",
	[10] = "SHL ",
	[11] = "DSYM",
	[12] = "INIT",
	[13] = "FINI",
	[14] = "PREI",
	[15] = "GRP ",
	[16] = "SIDX"
};

struct spad {
	int idx;
	int size;
	int addr;
};

void locate_strings_section(CTX)
{
	uint64_t shoff = ctx->shoff;
	int shstrndx = ctx->shstrndx;
	int shentsize = ctx->shentsize;

	if(!shstrndx)
		return; /* no .strings */

	void* sh = ctx->buf + shoff + shstrndx*shentsize;
	int elf64 = ctx->elf64;
	int elfxe = ctx->elfxe;

	uint32_t type;
	uint64_t offset, size;

	take_u32(elfshdr, sh, type);
	take_x64(elfshdr, sh, offset);
	take_x64(elfshdr, sh, size);

	if(type != SHT_STRTAB)
		return;

	use_strings_at_offset(ctx, offset, size);
}

static char* fmt_type(char* p, char* e, int type)
{
	if(type < ARRAY_SIZE(types)) {
		p = fmtraw(p, e, types[type], sizeof(*types));
	} else {
		p = fmtstr(p, e, "0x");
		p = fmtbyte(p, e, type);
	}

	return p;
}

static char* fmt_flags(char* p, char* e, ulong flags)
{
	p = fmtchar(p, e, flags & SHF_WRITE ? 'w' : '-');
	p = fmtchar(p, e, flags & SHF_ALLOC ? 'a' : '-');
	p = fmtchar(p, e, flags & SHF_EXECINSTR ? 'x' : '-');
	p = fmtchar(p, e, flags & SHF_MERGE ? 'm' : '-');

	return p;
}

static char* fmt_space(char* p, char* e, int n)
{
	while(n-- > 0 && p < e)
		*p++ = ' ';

	return p;
}

static void dump_section(CTX, int i, struct spad* sp)
{
	uint64_t shoff = ctx->shoff;
	uint16_t shentsize = ctx->shentsize;

	void* sh = ctx->buf + shoff + i*shentsize;
	int elf64 = ctx->elf64;
	int elfxe = ctx->elfxe;

	uint32_t name, type;
	uint64_t addr, size, flags;
	const char* namestr;

	take_u32(elfshdr, sh, name);
	take_u32(elfshdr, sh, type);
	take_x64(elfshdr, sh, addr);
	take_x64(elfshdr, sh, size);
	take_x64(elfshdr, sh, flags);

	if(!type) return;

	FMTBUF(p, e, buf, 100);

	p = fmtpad0(p, e, sp->idx, fmtint(p, e, i));
	p = fmtstr(p, e, "  ");
	p = fmt_type(p, e, type);
	p = fmtstr(p, e, " ");
	p = fmt_flags(p, e, flags);

	p = fmtstr(p, e, "  ");
	p = fmtpad(p, e, sp->size, fmtu64(p, e, size));

	if(addr) {
		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, "0x");
		p = fmtpad0(p, e, sp->addr, fmtx64(p, e, addr));
	} else if(sp->addr) {
		p = fmt_space(p, e, 3 + sp->addr);
	}

	if((namestr = lookup_string(ctx, name))) {
		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, namestr);
	}

	FMTENL(p, e);

	output(ctx, buf, p - buf);
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

static void prep_section_padding(CTX, struct spad* sp)
{
	uint64_t shoff = ctx->shoff;
	uint16_t shentsize = ctx->shentsize;
	int elf64 = ctx->elf64;
	int elfxe = ctx->elfxe;

	int i, shnum = ctx->shnum;
	uint64_t addr, maxaddr = 0;
	uint64_t size, maxsize = 0;

	for(i = 0; i < shnum; i++) {
		void* sh = ctx->buf + shoff + i*shentsize;

		take_x64(elfshdr, sh, addr);
		take_x64(elfshdr, sh, size);

		if(addr > maxaddr)
			maxaddr = addr;
		if(size > maxsize)
			maxsize = size;
	}

	sp->idx = dec_digits_in(shnum - 1);
	sp->size = dec_digits_in(maxsize);
	sp->addr = hex_digits_in(maxaddr);
}

void dump_sections_table(CTX)
{
	uint64_t shoff = ctx->shoff;
	int i, shnum = ctx->shnum;
	struct spad pad;

	if(!shoff)
		return warn("no sections in this file", NULL, 0);
	if(!shnum)
		return warn("empty sections table", NULL, 0);

	locate_strings_section(ctx);
	prep_section_padding(ctx, &pad);

	for(i = 0; i < shnum; i++)
		dump_section(ctx, i, &pad);
}
