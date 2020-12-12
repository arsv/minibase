#include <bits/elf.h>
#include <format.h>
#include <string.h>
#include <printf.h>
#include <util.h>
#include "elfinfo.h"

struct dpad {
	int idx;
};

struct phdr {
	uint64_t offset;
	uint64_t filesz;
};

struct dhdr {
	uint64_t tag;
	uint64_t val;
};

#define HEX 0
#define DEC 1
#define NOT 2
#define STR 3

static const struct tag {
	uint fmt;
	uint tag;
	const char* name;
} tags[] = {
	{ HEX, DT_NULL,         "NULL"         },
	{ STR, DT_NEEDED,       "NEEDED"       },
	{ HEX, DT_PLTRELSZ,     "PLTRELSZ"     },
	{ HEX, DT_PLTGOT,       "PLTGOT"       },
	{ HEX, DT_HASH,         "HASH"         },
	{ HEX, DT_STRTAB,       "STRTAB"       },
	{ HEX, DT_SYMTAB,       "SYMTAB"       },
	{ HEX, DT_RELA,         "RELA"         },
	{ DEC, DT_RELASZ,       "RELASZ"       },
	{ DEC, DT_RELAENT,      "RELAENT"      },
	{ DEC, DT_STRSZ,        "STRSZ"        },
	{ DEC, DT_SYMENT,       "SYMENT"       },
	{ HEX, DT_INIT,         "INIT"         },
	{ HEX, DT_FINI,         "FINI"         },
	{ STR, DT_SONAME,       "SONAME"       },
	{ STR, DT_RPATH,        "RPATH"        },
	{ HEX, DT_SYMBOLIC,     "SYMBOLIC"     },
	{ HEX, DT_REL,          "REL"          },
	{ DEC, DT_RELSZ,        "RELSZ"        },
	{ DEC, DT_RELENT,       "RELENT"       },
	{ HEX, DT_PLTREL,       "PLTREL"       },
	{ HEX, DT_DEBUG,        "DEBUG"        },
	{ HEX, DT_TEXTREL,      "TEXTREL"      },
	{ HEX, DT_JMPREL,       "JMPREL"       },
	{ NOT, DT_BIND_NOW,     "BIND_NOW"     },
	{ HEX, DT_INIT_ARRAY,   "INIT_ARRAY"   },
	{ HEX, DT_FINI_ARRAY,   "FINI_ARRAY"   },
	{ DEC, DT_INIT_ARRAYSZ, "INIT_ARRAYSZ" },
	{ DEC, DT_FINI_ARRAYSZ, "FINI_ARRAYSZ" },
	{ HEX, DT_RUNPATH,      "RUNPATH"      },
	{ HEX, DT_FLAGS,        "FLAGS"        },
	{ HEX, DT_ENCODING,     "ENCODING"     },
	{ HEX, 0x6ffffef5,      "GNU_HASH"     },
	{ HEX, 0x6ffffffc,      "VERDEF"       },
	{ HEX, 0x6ffffffb,      "FLAGS1"       },
	{ DEC, 0x6ffffffd,      "VERDEFNUM"    },
	{ HEX, 0x6ffffffe,      "VERNEED"      },
	{ DEC, 0x6fffffff,      "VERNEEDNUM"   },
	{ HEX, 0x6ffffff0,      "VERSYM"       },
	{ DEC, 0x6ffffff9,      "RELACOUNT"    },
};

static char* fmt_0x64(char* p, char* e, uint64_t val)
{
	p = fmtstr(p, e, "0x");
	p = fmtu64(p, e, val);

	return p;
}

static char* fmt_string(char* p, char* e, uint64_t val, CTX)
{
	const char* str;

	if((str = lookup_string(ctx, val))) {
		p = fmtstr(p, e, "\"");
		p = fmtstr(p, e, str);
		p = fmtstr(p, e, "\"");
	} else {
		p = fmt_0x64(p, e, val);
	}

	return p;
}

static void dump_dynamic_entry(CTX, struct dhdr* ph, struct dpad* pad, int i)
{
	const struct tag* tt;
	uint64_t tag = ph->tag;
	uint64_t val = ph->val;

	FMTBUF(p, e, buf, 100);

	p = fmtpad0(p, e, pad->idx, fmtint(p, e, i));
	p = fmtstr(p, e, "  ");

	for(tt = tags; tt < ARRAY_END(tags); tt++)
		if(tt->tag == tag)
			break;
	if(tt >= ARRAY_END(tags))
		tt = NULL;

	if(tt)
		p = fmtstr(p, e, tt->name);
	else
		p = fmt_0x64(p, e, tag);

	if(!tt || tt->fmt != NOT)
		p = fmtstr(p, e, " ");

	if(!tt || tt->fmt == HEX)
		p = fmt_0x64(p, e, val);
	else if(tt->fmt == DEC)
		p = fmtu64(p, e, val);
	else if(tt->fmt == STR)
		p = fmt_string(p, e, val, ctx);

	FMTENL(p, e);

	output(ctx, buf, p - buf);
}

static int dec_digits_in(uint x)
{
	int n = 1;

	while(x >= 10) { x /= 10; n++; }

	return n;
}

static int next_dyn_entry(CTX, struct phdr* ph, struct dhdr* dyn, int i)
{
	uint64_t offset = ph->offset;
	uint64_t filesz = ph->filesz;
	int elf64 = ctx->elf64;
	int elfxe = ctx->elfxe;

	int stride = elf64 ? sizeof(struct elf64dyn) : sizeof(struct elf32dyn);
	void* ptr = ctx->buf + offset + i*stride;
	void* end = ptr + filesz;

	if(ptr >= end)
		return -1;

	load_x64(dyn->tag, elfdyn, ptr, tag);
	load_x64(dyn->val, elfdyn, ptr, val);

	if(!dyn->tag)
		return -1;

	return i + 1;
}

static void scan_dyn_dump_entries(CTX, struct phdr* ph, struct dpad* pad)
{
	struct dhdr dyn, *dh = &dyn;
	int i = 0;

	while((i = next_dyn_entry(ctx, ph, dh, i)) >= 0)
		dump_dynamic_entry(ctx, dh, pad, i-1);
}

static void scan_dyn_locate_strings(CTX, struct phdr* ph)
{
	struct dhdr dyn, *dh = &dyn;
	uint64_t off = 0, len = 0;
	int i = 0;

	while((i = next_dyn_entry(ctx, ph, dh, i)) >= 0)
		if(dyn.tag == DT_STRTAB)
			off = dyn.val;
		else if(dyn.tag == DT_STRSZ)
			len = dyn.val;

	if(!off || !len)
		reset_strings_location(ctx);
	else
		use_strings_at_address(ctx, off, len);
}

static void prep_dyn_padding(CTX, struct phdr* ph, struct dpad* pad)
{
	int elf64 = ctx->elf64;
	uint64_t filesz = ph->filesz;
	int stride = elf64 ? sizeof(struct elf64dyn) : sizeof(struct elf32dyn);

	pad->idx = dec_digits_in(filesz / stride);
}

static void dump_dynamic_section(CTX, struct phdr* ph)
{
	uint64_t offset = ph->offset;
	uint64_t filesz = ph->filesz;
	uint64_t total = ctx->len;
	struct dpad pad;

	if(offset > total)
		return warn("invalid DYNAMIC entry", NULL, 0);
	if(offset + filesz > total)
		return warn("truncated DYNAMIC entry", NULL, 0);

	prep_dyn_padding(ctx, ph, &pad);

	scan_dyn_locate_strings(ctx, ph);
	scan_dyn_dump_entries(ctx, ph, &pad);
}

static int load_dyn_section(CTX, struct phdr* ph, int i)
{
	uint64_t phoff = ctx->phoff;
	uint16_t phentsize = ctx->phentsize;

	void* loc = ctx->buf + phoff + i*phentsize;
	int elf64 = ctx->elf64;
	int elfxe = ctx->elfxe;

	uint32_t type;

	take_u32(elfphdr, loc, type);

	if(type != PT_DYNAMIC) return -1;

	load_x64(ph->offset, elfphdr, loc, offset);
	load_x64(ph->filesz, elfphdr, loc, filesz);

	return 0;
}

void dump_dynamic_info(CTX)
{
	uint64_t phoff = ctx->phoff;
	int i, phnum = ctx->phnum;
	int seen_dynamic = 0;
	struct phdr hdr, *ph = &hdr;

	if(!phoff)
		return warn("no program table", NULL, 0);

	for(i = 0; i < phnum; i++) {
		if(load_dyn_section(ctx, ph, i))
			continue;

		if(seen_dynamic)
			warn("multiple dynamic sections", NULL, 0);
		else
			seen_dynamic = 1;

		dump_dynamic_section(ctx, ph);
	}

	if(!seen_dynamic)
		fail("not a dynamic executable", NULL, 0);
}

static void find_first_dynamic_entry(CTX, struct phdr* ph)
{
	uint64_t phoff = ctx->phoff;
	int i, phnum = ctx->phnum;

	if(!phoff) fail("no program header in this file", NULL, 0);

	for(i = 0; i < phnum; i++)
		if(load_dyn_section(ctx, ph, i) >= 0)
			return;

	fail("not a dynamic executable", NULL, 0);
}

void dump_dynamic_soname(CTX)
{
	struct phdr hdr, *ph = &hdr;
	struct dhdr dyn, *dh = &dyn;
	const char* str;
	int i = 0;

	find_first_dynamic_entry(ctx, ph);
	scan_dyn_locate_strings(ctx, ph);

	while((i = next_dyn_entry(ctx, ph, dh, i)) >= 0)
		if(dh->tag == DT_SONAME)
			break;
	if(i < 0)
		fail("no soname in this file", NULL, 0);

	if(!(str = lookup_string(ctx, dh->val)))
		fail("invalid soname value", NULL, 0);

	output(ctx, str, strlen(str));
	output(ctx, "\n", 1);
}

void dump_dynamic_libs(CTX)
{
	struct phdr hdr, *ph = &hdr;
	struct dhdr dyn, *dh = &dyn;
	const char* str;
	int i = 0, seen = 0;

	find_first_dynamic_entry(ctx, ph);
	scan_dyn_locate_strings(ctx, ph);

	while((i = next_dyn_entry(ctx, ph, dh, i)) >= 0) {
		if(dh->tag != DT_NEEDED)
			continue;

		seen = 1;

		if(!(str = lookup_string(ctx, dh->val))) {
			warn("invalid library entry", NULL, i - 1);
			continue;
		}

		output(ctx, str, strlen(str));
		output(ctx, "\n", 1);
	}

	if(!seen) fail("no library references in this file", NULL, 0);
}

int got_any_dynamic_entries(CTX)
{
	uint64_t phoff = ctx->phoff;
	int i, phnum = ctx->phnum;
	struct phdr hdr;

	if(!phoff) return 0;

	for(i = 0; i < phnum; i++)
		if(load_dyn_section(ctx, &hdr, i) >= 0)
			return 1;

	return 0;
}
