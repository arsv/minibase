#include <bits/elf.h>
#include <format.h>
#include <string.h>
#include <printf.h>
#include <util.h>
#include "elfinfo.h"

struct phdr {
	uint32_t type;
	uint32_t flags;

	uint64_t offset;
	uint64_t vaddr;
	uint64_t filesz;
	uint64_t memsz;
};

struct ppad {
	int idx;
	int addr;
};

static const char* const types[] = {
	[0] = "NULL",
	[1] = "LOAD",
	[2] = "DYNAMIC",
	[3] = "INTERP",
	[4] = "NOTE",
	[5] = "SHLIB",
	[6] = "PHDR",
	[7] = "TLS",
	[8] = "NUM"
};

static const char* typename(uint type)
{
	if(type < ARRAY_SIZE(types))
		return types[type];

	if(type == 0x6474e550)
		return "EHFRAME";
	if(type == 0x6474e551)
		return "STACK";
	if(type == 0x6474e552)
		return "RELRO";

	return NULL;
}

static char* fmt_type(char* p, char* e, struct phdr* ph)
{
	uint type = ph->type;
	const char* name;

	if((name = typename(type)))
		return fmtstr(p, e, name);

	p = fmtstr(p, e, "0x");
	p = fmthex(p, e, type);

	return p;
}

static char* fmt_size(char* p, char* e, struct phdr* ph)
{
	uint64_t filesz = ph->filesz;
	uint64_t memsz = ph->memsz;

	p = fmtstr(p, e, "size ");
	p = fmtu64(p, e, filesz);

	if(memsz == filesz)
		return p;

	if(memsz < filesz) {
		p = fmtstr(p, e, "memsz ");
		p = fmtu64(p, e, memsz);
	} else {
		p = fmtstr(p, e, " bss ");
		p = fmtu64(p, e, memsz - filesz);
	}

	return p;
}

static char* fmt_unknown(char* p, char* e, struct phdr* ph)
{
	uint64_t filesz = ph->filesz;
	uint64_t memsz = ph->memsz;

	if(memsz == filesz) {
		p = fmtu64(p, e, filesz);
		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, filesz == 1 ? "byte" : "bytes");
	} else  {
		p = fmtstr(p, e, "size ");
		p = fmtu64(p, e, filesz);

		if(memsz > filesz) {
			p = fmtstr(p, e, " bss ");
			p = fmtu64(p, e, memsz - filesz);
		} else {
			p = fmtstr(p, e, " memsz ");
			p = fmtu64(p, e, memsz);
		}
	}

	return p;
}

static char* fmt_flags(char* p, char* e, struct phdr* ph)
{
	int flags = ph->flags;

	p = fmtchar(p, e, flags & PF_R ? 'r' : '-');
	p = fmtchar(p, e, flags & PF_W ? 'w' : '-');
	p = fmtchar(p, e, flags & PF_X ? 'x' : '-');

	return p;
}

static char* fmt_load(char* p, char* e, struct phdr* ph, struct ppad* pp)
{
	p = fmt_flags(p, e, ph);
	p = fmtstr(p, e, " ");

	p = fmtstr(p, e, "0x");
	p = fmtpad0(p, e, pp->addr, fmtx64(p, e, ph->vaddr));
	p = fmtstr(p, e, " ");
	p = fmt_size(p, e, ph);

	return p;
}

static char* fmt_interp(char* p, char* e, struct phdr* ph, CTX)
{
	uint64_t offset = ph->offset;
	uint64_t filesz = ph->filesz;
	uint64_t total = ctx->len;

	if(filesz < 2)
		return fmtstr(p, e, "(empty)");
	if(offset > total)
		return fmtstr(p, e, "(invalid)");
	if(offset + filesz > total)
		filesz = total - offset;

	void* ss = ctx->buf + offset;

	p = fmtstrn(p, e, ss, filesz);

	if(filesz < ph->filesz)
		p = fmtstr(p, e, " (truncated)");

	return p;
}

static void dump_interp_string(CTX, struct phdr* ph)
{
	uint64_t offset = ph->offset;
	uint64_t filesz = ph->filesz;
	uint64_t total = ctx->len;

	if(filesz < 2)
		fail("empty interp", NULL, 0);
	if(offset > total)
		fail("invalid interp", NULL, 0);
	if(offset + filesz > total)
		fail("truncated interp", NULL, 0);

	void* ss = ctx->buf + offset;
	int len = strnlen(ss, filesz);

	output(ctx, ss, len);
	output(ctx, "\n", 1);
}

static void load_phdr(CTX, struct phdr* ph, int i)
{
	uint64_t phoff = ctx->phoff;
	uint16_t phentsize = ctx->phentsize;

	void* loc = ctx->buf + phoff + i*phentsize;
	int elf64 = ctx->elf64;
	int elfxe = ctx->elfxe;

	memzero(ph, sizeof(*ph));

	copy_u32(elfphdr, loc, type,   &ph->type);
	copy_u32(elfphdr, loc, flags,  &ph->flags);

	copy_x64(elfphdr, loc, offset, &ph->offset);
	copy_x64(elfphdr, loc, vaddr,  &ph->vaddr);
	copy_x64(elfphdr, loc, filesz, &ph->filesz);
	copy_x64(elfphdr, loc, memsz,  &ph->memsz);
}

static void dump_prentry(CTX, int i, struct ppad* pp)
{
	struct phdr phdr, *ph = &phdr;

	load_phdr(ctx, ph, i);

	if(!ph->type) return;

	FMTBUF(p, e, buf, 100);

	p = fmtpad0(p, e, pp->idx, fmtint(p, e, i));
	p = fmtstr(p, e, "  ");

	p = fmt_type(p, e, ph);
	p = fmtstr(p, e, " ");

	switch(ph->type) {
		case PT_LOAD:   p = fmt_load(p, e, ph, pp); break;
		case PT_INTERP: p = fmt_interp(p, e, ph, ctx); break;
		default:        p = fmt_unknown(p, e, ph);
	}

	FMTENL(p, e);

	output(ctx, buf, p - buf);
}

static uint32_t get_phdr_type(CTX, int i)
{
	uint64_t phoff = ctx->phoff;
	uint16_t phentsize = ctx->phentsize;

	void* loc = ctx->buf + phoff + i*phentsize;
	int elf64 = ctx->elf64;
	int elfxe = ctx->elfxe;

	uint32_t type;

	take_u32(elfphdr, loc, type);

	return type;
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

static void prep_program_padding(CTX, struct ppad* pp)
{
	uint64_t phoff = ctx->phoff;
	uint16_t phentsize = ctx->phentsize;
	int elf64 = ctx->elf64;
	int elfxe = ctx->elfxe;

	int i, phnum = ctx->phnum;
	uint32_t type;
	uint64_t vaddr, maxvaddr = 0;
	//uint64_t size, maxsize = 0;

	for(i = 0; i < phnum; i++) {
		void* ph = ctx->buf + phoff + i*phentsize;

		take_u32(elfphdr, ph, type);
		take_x64(elfphdr, ph, vaddr);

		if(type != PT_LOAD)
			continue;
		if(vaddr > maxvaddr)
			maxvaddr = vaddr;
	}

	pp->idx = dec_digits_in(phnum - 1);
	pp->addr = hex_digits_in(maxvaddr);
	//sp->size = dec_digits_in(maxsize);
}

void dump_program_header(CTX)
{
	uint64_t phoff = ctx->phoff;
	int i, phnum = ctx->phnum;
	struct ppad pad;

	if(!phoff)
		return warn("no program table in this file", NULL, 0);
	if(!phnum)
		return warn("empty program table", NULL, 0);

	prep_program_padding(ctx, &pad);

	for(i = 0; i < phnum; i++)
		dump_prentry(ctx, i, &pad);
}

void dump_program_interp(CTX)
{
	uint64_t phoff = ctx->phoff;
	int i, phnum = ctx->phnum;
	struct phdr phdr, *ph = &phdr;

	if(!phoff)
		return warn("no program table in this file", NULL, 0);
	if(!phnum)
		return warn("empty program table", NULL, 0);

	for(i = 0; i < phnum; i++)
		if(get_phdr_type(ctx, i) == PT_INTERP)
			break;
	if(i >= phnum)
		fail("no program interpreter", NULL, 0);

	load_phdr(ctx, ph, i);

	dump_interp_string(ctx, ph);
}
