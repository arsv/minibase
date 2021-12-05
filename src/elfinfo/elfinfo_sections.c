#include <bits/elf.h>
#include <string.h>
#include <util.h>

#include "elfinfo.h"

static const char* types[] = {
	 [0] = "NULL",
	 [1] = "PROGBITS",
	 [2] = "SYMTAB",
	 [3] = "STRTAB",
	 [4] = "RELA",
	 [5] = "HASH",
	 [6] = "DYNAMIC",
	 [7] = "NOTE",
	 [8] = "NOBITS",
	 [9] = "REL",
	[10] = "SHLIB",
	[11] = "DYNSYM",
	[14] = "INIT_ARRAY",
	[15] = "FINI_ARRAY",
	[16] = "PREINIT_ARRAY",
	[17] = "GROUP",
	[18] = "SYMTAB_SHNDX",
	[19] = "NUM"
};

static const char* gnu_types[] = {
	[0x5] = "GNU_ATTRIBUTES",
	[0x6] = "GNU_HASH",
	[0x7] = "GNU_LIBLIST",
	[0x8] = "CHECKSUM",
	[0xD] = "GNU_VERDEF",
	[0xE] = "GNU_VERNEED",
	[0xF] = "GNU_VERSYM"
};

static const char* shf_names[] = {
	 [0] = "WRITE",
	 [1] = "ALLOC",
	 [2] = "EXECINSTR",
	 [4] = "MERGE",
	 [5] = "STRINGS",
	 [6] = "INFO_LINK",
	 [7] = "LINK_ORDER",
	 [8] = "OS_NONSTD",
	 [9] = "GROUP",
	[10] = "TLS",
	[11] = "COMPRESSED"
};

static const char* type_name(uint type)
{
	const char* name;

	if(type < ARRAY_SIZE(types))
		if((name = types[type]))
			return name;

	if((type >= 0x6ffffff0) && (type <= 0x6fffffff)) {
		uint idx = type - 0x6ffffff0;

		if((name = gnu_types[idx]))
			return name;
	}

	return NULL;
}

static void print_type(uint type)
{
	const char* name;

	if((name = type_name(type)))
		print(name);
	else
		print_hex(type);
}

static void print_link(uint link)
{
	if(!link) return;

	print(" link=");
	print_int(link);
}

static const char flag_table[] = "WAX?MSIDNGTC";

static void print_sflags(uint flags)
{
	uint i, n = 32;

	if(!flags)
		return;

	print(" ");

	for(i = 0; i < n; i++) {
		if(!(flags & (1<<i)))
			continue;

		if(i < sizeof(flag_table))
			print_char(flag_table[i]);
		else
			print_char('?');
	}
}

static void print_flags(uint v)
{
	uint i;
	uint shfn = ARRAY_SIZE(shf_names);
	const char* fn;

	print_hex(v);

	for(i = 0; i < 32; i++) {
		if(!(v & (1<<i)))
			continue;
		if(i >= shfn)
			break;
		if(!(fn = shf_names[i]))
			continue;

		print(" ");
		print(fn);

		v &= ~(1<<i);
	} if(v) {
		print(" ");
		print_hex(v);
	}
}

static void dump_section_64(uint i)
{
	struct elf64shdr* sh = get_shent_64(i);

	uint noff = F.ldw(&sh->name);
	uint type = F.ldw(&sh->type);
	uint link = F.ldw(&sh->link);
	uint flags = F.ldx(&sh->flags);

	if(i == 0 && !noff)
		return;

	print_idx(i, E.shnum);
	print(" ");
	print_type(type);
	print(" ");
	print_strn(noff);
	print_sflags(flags);
	print_link(link);

	print_end();
}

static void dump_section_32(uint i)
{
	struct elf32shdr* sh = get_shent_32(i);

	uint noff = F.ldw(&sh->name);
	uint type = F.ldw(&sh->type);
	uint link = F.ldw(&sh->link);
	uint flags = F.ldw(&sh->flags);

	if(i == 0 && !noff)
		return;

	print_idx(i, E.shnum);
	print(" ");
	print_type(type);
	print(" ");
	print_strn(noff);
	print_sflags(flags);
	print_link(link);

	print_end();
}

void dump_sections_table(void)
{
	int i, n = E.shnum;

	no_more_arguments();

	if(!n) fail("empty program table", NULL, 0);

	use_strings_from(E.shstrndx);

	for(i = 0; i < n; i++) {
		if(elf64)
			dump_section_64(i);
		else
			dump_section_32(i);
	}
}

void print_shdr_name(uint* v)
{
	print_tag("name");
	print_strx(F.ldw(v));
	print_end();
}

static void print_shdr_type(uint32_t* v)
{
	uint type = F.ldw(v);
	const char* name;

	print_tag("type");
	print_hex(type);

	if((name = type_name(type))) {
		print(" ");
		print(name);
	}

	print_end();
}

static void print_shdr_flags(uint64_t flags)
{
	print_tag("flags");

	if(flags >> 32)
		print_x64(flags);
	else
		print_flags(flags);

	print_end();
}

static void dump_single_shdr_64(uint shndx)
{
	struct elf64shdr* sh = get_shent_64(shndx);
	uint64_t flags = F.ldx(&sh->flags);

	print_shdr_name(&sh->name);
	print_shdr_type(&sh->type);
	print_shdr_flags(flags);

	print_tag_hex64("addr", &sh->addr);
	print_tag_dec64("offset", &sh->offset);
	print_tag_dec64("size", &sh->size);
	print_tag_dec32("link", &sh->link);
	print_tag_dec32("info", &sh->info);
	print_tag_dec64("align", &sh->align);
	print_tag_dec64("entsize", &sh->entsize);
}

static void dump_single_shdr_32(uint shndx)
{
	struct elf32shdr* sh = get_shent_32(shndx);
	uint flags = F.ldw(&sh->flags);

	print_shdr_name(&sh->name);
	print_shdr_type(&sh->type);
	print_shdr_flags(flags);

	print_tag_hex32("addr", &sh->addr);
	print_tag_dec32("offset", &sh->offset);
	print_tag_dec32("size", &sh->size);
	print_tag_dec32("link", &sh->link);
	print_tag_dec32("info", &sh->info);
	print_tag_dec32("align", &sh->align);
	print_tag_dec32("entsize", &sh->entsize);
}

void dump_single_shdr(void)
{
	uint shndx = shift_int();

	no_more_arguments();

	use_strings_from_shstrtab();

	if(elf64)
		dump_single_shdr_64(shndx);
	else
		dump_single_shdr_32(shndx);
}

