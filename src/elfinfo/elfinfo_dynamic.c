#include <bits/elf.h>
#include <string.h>
#include <util.h>

#include "elfinfo.h"

#define HEX 0
#define STR 1
#define DEC 2

static const char* tag_names[] = {
	[0] = "NULL",
	[1] = "NEEDED",
	[2] = "PLTRELSZ",
	[3] = "PLTGOT",
	[4] = "HASH",
	[5] = "STRTAB",
	[6] = "SYMTAB",
	[7] = "RELA",
	[8] = "RELASZ",
	[9] = "RELAENT",
	[10] = "STRSZ",
	[11] = "SYMENT",
	[12] = "INIT",
	[13] = "FINI",
	[14] = "SONAME",
	[15] = "RPATH",
	[16] = "SYMBOLIC",
	[17] = "REL",
	[18] = "RELSZ",
	[19] = "RELENT",
	[20] = "PLTREL",
	[21] = "DEBUG",
	[22] = "TEXTREL",
	[23] = "JMPREL",
	[24] = "BIND_NOW",
	[25] = "INIT_ARRAY",
	[26] = "FINI_ARRAY",
	[27] = "INIT_ARRAYSZ",
	[28] = "FINI_ARRAYSZ",
	[29] = "RUNPATH",
	[30] = "FLAGS",
	[31] = NULL,
	[32] = "DT_PREINIT_ARRAY",
	[33] = "DT_PREINIT_ARRAYSZ",
	[34] = "SYMTAB_SHNDX"
};

static const struct gnu_tag {
	uint tag;
	char* name;
	uint fmt;
} gnu_tags[] = {
	{ 0x6ffffef5,      "GNU_HASH",      HEX },
	{ 0x6ffffffc,      "VERDEF",        HEX },
	{ 0x6ffffffb,      "FLAGS1",        HEX },
	{ 0x6ffffffd,      "VERDEFNUM",     DEC },
	{ 0x6ffffffe,      "VERNEED",       HEX },
	{ 0x6fffffff,      "VERNEEDNUM",    DEC },
	{ 0x6ffffff0,      "VERSYM",        HEX },
	{ 0x6ffffff9,      "RELACOUNT",     DEC },
};

static char* gnu_name(uint tag)
{
	const struct gnu_tag* tt;

	for(tt = gnu_tags; tt < ARRAY_END(gnu_tags); tt++)
		if(tag == tt->tag)
			return tt->name;

	return NULL;
}

static int decide_format_gnu(uint tag)
{
	const struct gnu_tag* tt;

	if((tag >> 24) != 0x6F)
		return HEX;

	for(tt = gnu_tags; tt < ARRAY_END(gnu_tags); tt++)
		if(tag == tt->tag)
			return tt->fmt;

	return HEX;
}

static void print_dynkey(uint tag)
{
	const char* name;

	if(tag < ARRAY_SIZE(tag_names) && (name = tag_names[tag]))
		return print(name);
	if((name = gnu_name(tag)))
		return print(name);

	print_hex(tag);
}

static int decide_tag_format(uint tag)
{
	if(tag > 32)
		return decide_format_gnu(tag);

	switch(tag) {
		case DT_NEEDED: return STR;
		case DT_PLTRELSZ: return DEC;
		case DT_RELASZ: return DEC;
		case DT_RELAENT: return DEC;
		case DT_STRSZ: return DEC;
		case DT_SYMENT: return DEC;
		case DT_SONAME: return STR;
		case DT_RPATH: return STR;
		case DT_RELSZ: return DEC;
		case DT_RELENT: return DEC;
		case DT_INIT_ARRAYSZ: return DEC;
		case DT_FINI_ARRAYSZ: return DEC;
		case DT_RUNPATH: return STR;
		default: return HEX;
	}
}

static void dump_dynamic_entry(uint64_t tag, uint64_t val)
{
	if((tag >> 32) || (val >> 32)) {
		print_x64(tag);
		print(" ");
		print_x64(val);
	} else {
		uint tt = (uint)tag;
		uint vv = (uint)val;
		uint ff = decide_tag_format(tt);

		print_dynkey(tt);
		print(" ");

		if(ff == STR)
			print_strq(vv);
		if(ff == DEC)
			print_u64(vv);
		if(ff == HEX)
			print_x64(vv);
	}

	print_end();
}

static void walk_dynamic_64(void)
{
	struct elf64dyn* dd = range(E.dynoff, E.dynlen);
	uint i, n = E.dynlen / sizeof(*dd);

	for(i = 0; i < n; i++) {
		struct elf64dyn* dyn = &dd[i];

		uint tag = F.ldx(&dyn->tag);
		uint val = F.ldx(&dyn->val);

		dump_dynamic_entry(tag, val);
	}
}

static void walk_dynamic_32(void)
{
	struct elf32dyn* dd = range(E.dynoff, E.dynlen);
	uint i, n = E.dynlen / sizeof(*dd);

	for(i = 0; i < n; i++) {
		struct elf32dyn* dyn = &dd[i];

		uint tag = F.ldw(&dyn->tag);
		uint val = F.ldw(&dyn->val);

		dump_dynamic_entry(tag, val);
	}
}

static void print_dynval(uint off)
{
	char* str = string(off);

	if(!str) return;

	print(str);

	print_end();
}

static uint scan_dump_64(uint type)
{
	struct elf64dyn* dd = range(E.dynoff, E.dynlen);
	uint i, n = E.dynlen / sizeof(*dd);
	uint found = 0;

	for(i = 0; i < n; i++) {
		struct elf64dyn* dyn = &dd[i];

		uint tag = F.ldx(&dyn->tag);
		uint val = F.ldx(&dyn->val);

		if(tag != type)
			continue;

		print_dynval(val);

		found++;
	}

	return found;
}

static uint scan_dump_32(uint type)
{
	struct elf32dyn* dd = range(E.dynoff, E.dynlen);
	uint i, n = E.dynlen / sizeof(*dd);
	uint found = 0;

	for(i = 0; i < n; i++) {
		struct elf32dyn* dyn = &dd[i];

		uint tag = F.ldw(&dyn->tag);
		uint val = F.ldw(&dyn->val);

		if(tag != type)
			continue;

		print_dynval(val);

		found++;
	}

	return found;
}

static void locate_dynamic_section(void)
{
	uint link;

	if(elf64) {
		struct elf64shdr* sh = find_section_64(SHT_DYNAMIC);

		if(!sh) fail("no DYNAMIC section", NULL, 0);

		E.dynoff = F.ldx(&sh->offset);
		E.dynlen = F.ldx(&sh->size);

		link = F.ldw(&sh->link);
	} else {
		struct elf32shdr* sh = find_section_32(SHT_DYNAMIC);

		if(!sh) fail("no DYNAMIC section", NULL, 0);

		E.dynoff = F.ldw(&sh->offset);
		E.dynlen = F.ldw(&sh->size);

		link = F.ldw(&sh->link);
	};

	use_strings_from(link);
}

void dump_dynamic_table(void)
{
	locate_dynamic_section();

	if(elf64)
		walk_dynamic_64();
	else
		walk_dynamic_32();
}

static void scan_dump_strings(uint type)
{
	uint ret;

	locate_dynamic_section();

	if(elf64)
		ret = scan_dump_64(type);
	else
		ret = scan_dump_32(type);

	if(!ret) fail("no entries found", NULL, 0);
}

void dump_dynamic_soname(void)
{
	scan_dump_strings(DT_SONAME);
}

void dump_dynamic_libs(void)
{
	scan_dump_strings(DT_NEEDED);
}
