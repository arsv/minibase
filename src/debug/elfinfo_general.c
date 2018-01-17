#include <bits/elf.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "elfinfo.h"

#define LE 1
#define BE 2

static const struct machine {
	int key;
	char* name;
	int bits;
	int order;
} machines[] = {
	{ 62,  "x86-64",    64, LE },
	{ 3,   "x86",       32, LE },
	{ 40,  "ARM",       32, LE },
	{ 183, "ARM64",     64, LE },
	{ 20,  "PowerPC",   32,  0 },
	{ 21,  "PPC64",     64,  0 },
	{ 2,   "SPARC",      0, BE },
	{ 8,   "MIPS",       0, BE },
	{ 83,  "AVR",       32,  0 },
	{ 92,  "OpenRISC",   0, LE },
	{ 189, "MicroBlaze", 0,  0 },
	{ 93,  "ARC",       32,  0 },
	{ 94,  "Xtensa",    32,  0 },
	{ 106, "Blackfin",  32,  0 },
	{ 224, "AMD GPU",   32,  0 },
	{ 243, "RISC-V",     0, LE }
};

static char* fmt_machine(char* p, char* e, int machine, CTX)
{
	const struct machine* mm;
	int bits = 0, gotbits = ctx->elf64 ? 64 : 32;
	int order = 0, gotorder = ctx->bigendian ? BE : LE;

	for(mm = machines; mm < ARRAY_END(machines); mm++)
		if(mm->key == machine)
			break;

	if(mm < ARRAY_END(machines)) {
		p = fmtstr(p, e, mm->name);

		if(mm->bits != gotbits)
			bits = gotbits;
		if(mm->order != gotorder)
			order = gotorder;
	} else {
		p = fmtstr(p, e, "machine ");
		p = fmtstr(p, e, "0x");
		p = fmtbyte(p, e, machine & 0xFF);
	}

	if(bits || order)
		p = fmtstr(p, e, " (");
	if(bits) {
		p = fmtint(p, e, bits);
		p = fmtstr(p, e, "bit");
	}
	if(bits && order)
		p = fmtstr(p, e, ", ");
	if(order)
		p = fmtstr(p, e, order == BE ? "BE" : "LE");
	if(bits || order)
		p = fmtstr(p, e, ")");

	return p;
}

static const char* const types[] = {
	[0] = "null-type",
	[1] = "object file",
	[2] = "executable",
	[3] = "dynlibrary",
	[4] = "core dump"
};

static char* fmt_type(char* p, char* e, uint type)
{
	if(type < ARRAY_SIZE(types))
		return fmtstr(p, e, types[type]);

	p = fmtstr(p, e, "type ");
	p = fmtint(p, e, type);

	return p;
}

static char* fmt_linkage(char* p, char* e, CTX)
{
	if(ctx->elftype != ET_EXEC)
		return p;

	p = fmtstr(p, e, ", ");

	if(got_any_dynamic_entries(ctx))
		return fmtstr(p, e, "dynamically linked");
	else
		return fmtstr(p, e, "statically linked");
}

void dump_general_info(CTX)
{
	void* eh = ctx->buf;
	int elf64 = ctx->elf64;
	int elfxe = ctx->elfxe;
	uint16_t type, machine;

	take_u16(elfhdr, eh, type);
	take_u16(elfhdr, eh, machine);

	FMTBUF(p, e, buf, 100);
	p = fmtstr(p, e, "ELF");
	p = fmtstr(p, e, " ");
	p = fmt_type(p, e, type);
	p = fmtstr(p, e, ", ");
	p = fmt_machine(p, e, machine, ctx);
	p = fmtstr(p, e, ", ");
	p = fmtint(p, e, ctx->shnum ? ctx->shnum - 1 : 0);
	p = fmtstr(p, e, ctx->shnum == 2 ? " section" : " sections");
	p = fmt_linkage(p, e, ctx);
	FMTENL(p, e);

	output(ctx, buf, p - buf);
}
