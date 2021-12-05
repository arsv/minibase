#include <bits/elf.h>
#include <string.h>
#include <util.h>

#include "elfinfo.h"

static const char binds[] = "LGWN";

static const char types[][3] = {
	 [0] = "-N-",
	 [1] = "OBJ",
	 [2] = "FUN",
	 [3] = "SEC",
	 [4] = "FIL",
	 [5] = "COM",
	 [6] = "TLS",
	 [7] = "NUM",
	[10] = "IFN"
};

static const char* long_types[] = {
	 [0] = "NOTYPE",
	 [1] = "OBJECT",
	 [2] = "FUNC",
	 [3] = "SECTION",
	 [4] = "FILE",
	 [5] = "COMMON",
	 [6] = "TLS",
	 [7] = "NUM",
	[10] = "GNU_IFUNC",
};

static const char* long_binds[] = {
	 [0] = "LOCAL",
	 [1] = "GLOBAL",
	 [2] = "WEAK",
	 [3] = "NUM",
};

static void print_type(uint info)
{
	uint type = ELF_SYM_TYPE(info);
	uint bind = ELF_SYM_BIND(info);

	if(bind < sizeof(binds))
		print_char(binds[bind]);
	else
		print_char('?');

	print(" ");

	if(type < ARRAY_SIZE(types) && types[type][0])
		print_raw(types[type], 3);
	else
		print_raw("???", 3);
}

static void symbol_line_64(uint i, struct elf64sym* sym)
{
	print_idx(i, E.symnum);
	print(" ");
	print_type(sym->info);
	print(" ");
	print_strn(F.ldw(&sym->name));
	print_end();
}

static void symbol_line_32(uint i, struct elf32sym* sym)
{
	print_idx(i, E.symnum);
	print(" ");
	print_type(sym->info);
	print(" ");
	print_strn(F.ldw(&sym->name));
	print_end();
}

static void section_line_64(uint shndx)
{
	struct elf64shdr* sh = get_shent_64(shndx);

	print("# section ");
	print_int(shndx);
	print(" ");
	print_strn(F.ldw(&sh->name));
	print_end();
}

static void section_line_32(uint shndx)
{
	struct elf32shdr* sh = get_shent_32(shndx);

	print("# section ");
	print_int(shndx);
	print(" ");
	print_strq(F.ldw(&sh->name));
	print_end();
}

static int section_has_symbols_64(uint shndx)
{
	for(uint i = 0; i < E.symnum; i++) {
		struct elf64sym* sym = get_sym_64(i);

		if(shndx == F.lds(&sym->shndx))
			return shndx;
	}

	return 0;
}

static int section_has_symbols_32(uint shndx)
{
	for(uint i = 0; i < E.symnum; i++) {
		struct elf32sym* sym = get_sym_32(i);

		if(shndx == F.lds(&sym->shndx))
			return shndx;
	}

	return 0;
}

static void list_section_symbols_64(uint shndx)
{
	if(!section_has_symbols_64(shndx))
		return;

	use_strings_from_shstrtab();

	section_line_64(shndx);

	use_strings_from_symstr();

	for(uint i = 0; i < E.symnum; i++) {
		struct elf64sym* sym = get_sym_64(i);

		if(shndx != F.lds(&sym->shndx))
			continue;

		symbol_line_64(i, sym);
	}
}

static void list_section_symbols_32(uint shndx)
{
	if(!section_has_symbols_32(shndx))
		return;

	use_strings_from_shstrtab();

	section_line_32(shndx);

	use_strings_from_symstr();

	for(uint i = 0; i < E.symnum; i++) {
		struct elf32sym* sym = get_sym_32(i);

		if(shndx != F.lds(&sym->shndx))
			continue;

		symbol_line_32(i, sym);
	}
}

static void locate_symtab_64(void)
{
	struct elf64shdr* sh = find_section_64(SHT_SYMTAB);

	if(!sh) fail("no SYMTAB in this file", NULL, 0);

	E.symoff = F.ldx(&sh->offset);
	E.symlen = F.ldx(&sh->size);
	E.symstr = F.ldw(&sh->link);

	struct elf64sym* ss = range(E.symoff, E.symlen);

	E.symnum = E.symlen / sizeof(*ss);
}

static void locate_symtab_32(void)
{
	struct elf32shdr* sh = find_section_32(SHT_SYMTAB);

	if(!sh) fail("no SYMTAB in this file", NULL, 0);

	E.symoff = F.ldw(&sh->offset);
	E.symlen = F.ldw(&sh->size);
	E.symstr = F.ldw(&sh->link);

	struct elf32sym* ss = range(E.symoff, E.symlen);

	E.symnum = E.symlen / sizeof(*ss);
}

void dump_symbol_table(void)
{
	no_more_arguments();

	if(elf64)
		locate_symtab_64();
	else
		locate_symtab_32();

	use_strings_from_symstr();

	for(uint i = 0; i < E.symnum; i++) {
		if(elf64)
			symbol_line_64(i, get_sym_64(i));
		else
			symbol_line_32(i, get_sym_32(i));
	}
}

static void locate_dynsym_64(void)
{
	struct elf64shdr* sh = find_section_64(SHT_DYNSYM);

	if(!sh) fail("no DYNSYM in this file", NULL, 0);

	E.symoff = F.ldx(&sh->offset);
	E.symlen = F.ldx(&sh->size);
	E.symstr = F.ldw(&sh->link);

	struct elf64sym* ss = range(E.symoff, E.symlen);

	E.symnum = E.symlen / sizeof(*ss);
}

static void locate_dynsym_32(void)
{
	struct elf32shdr* sh = find_section_32(SHT_DYNSYM);

	if(!sh) fail("no DYNSYM in this file", NULL, 0);

	E.symoff = F.ldw(&sh->offset);
	E.symlen = F.ldw(&sh->size);
	E.symstr = F.ldw(&sh->link);

	struct elf32sym* ss = range(E.symoff, E.symlen);

	E.symnum = E.symlen / sizeof(*ss);
}

void dump_dynsym_table(void)
{
	if(elf64)
		locate_dynsym_64();
	else
		locate_dynsym_32();

	use_strings_from_symstr();

	for(uint i = 0; i < E.symnum; i++) {
		if(elf64)
			symbol_line_64(i, get_sym_64(i));
		else
			symbol_line_32(i, get_sym_32(i));
	}
}

void dump_section_symbols(void)
{
	no_more_arguments();

	if(elf64)
		locate_symtab_64();
	else
		locate_symtab_32();

	for(uint i = 1; i < E.shnum; i++) {
		if(elf64)
			list_section_symbols_64(i);
		else
			list_section_symbols_32(i);
	}
}

static uint section_noff(uint shndx)
{
	if(elf64) {
		struct elf64shdr* sh = get_shent_64(shndx);
		return F.ldw(&sh->name);
	} else {
		struct elf32shdr* sh = get_shent_32(shndx);
		return F.ldw(&sh->name);
	}
}

static void print_sym_name(uint* v)
{
	use_strings_from_symstr();

	print_tag("name");
	print_strx(F.ldw(v));
	print_end();
}

static void print_sym_shndx(uint16_t* v)
{
	uint shndx = F.lds(v);

	use_strings_from_shstrtab();

	print_tag("shndx");

	if(shndx && (shndx < E.shnum)) {
		print_int(shndx);
		print(" ");
		print_strq(section_noff(shndx));
	} else if(shndx < 0xFF00) {
		print_int(shndx);
	} else {
		print_hex(shndx);
	}

	print_end();
}

static void print_sym_other(byte* v)
{
	print_tag("other");
	print_hex(*v);
	print_end();
}

static const char* long_bind_name(uint bind)
{
	if(bind >= ARRAY_SIZE(long_binds))
		return NULL;

	return long_binds[bind];
}

static const char* long_type_name(uint type)
{
	if(type >= ARRAY_SIZE(long_types))
		return NULL;

	return long_types[type];
}

static void print_sym_info(byte* v)
{
	uint info = *v;

	uint type = ELF_SYM_TYPE(info);
	uint bind = ELF_SYM_BIND(info);
	const char* str;

	print_tag("info");
	print_hex(info);

	if((str = long_bind_name(bind))) {
		print(" ");
		print(str);
	} else {
		print(" bind ");
		print_int(bind);
	}

	if((str = long_type_name(type))) {
		print(" ");
		print(str);
	} else {
		print(" type ");
		print_int(bind);
	}

	print_end();
}

static void print_symbol_64(uint i)
{
	struct elf64sym* sym = get_sym_64(i);

	print_sym_name(&sym->name);
	print_sym_info(&sym->info);
	print_sym_other(&sym->other);
	print_sym_shndx(&sym->shndx);
	print_tag_hex64("value", &sym->value);
	print_tag_dec64("size", &sym->size);
}

static void print_symbol_32(uint i)
{
	struct elf32sym* sym = get_sym_32(i);

	print_sym_name(&sym->name);
	print_tag_hex32("value", &sym->value);
	print_tag_dec32("size", &sym->size);
	print_sym_info(&sym->info);
	print_sym_other(&sym->other);
	print_sym_shndx(&sym->shndx);
}

void dump_single_sym(void)
{
	uint i = shift_int();

	no_more_arguments();

	if(elf64)
		locate_symtab_64();
	else
		locate_symtab_32();

	if(i >= E.symnum)
		fail("index out of range", NULL, 0);

	if(elf64)
		print_symbol_64(i);
	else
		print_symbol_32(i);
}

void dump_single_dsym(void)
{
	uint i = shift_int();

	no_more_arguments();

	if(elf64)
		locate_dynsym_64();
	else
		locate_dynsym_32();

	if(i >= E.symnum)
		fail("index out of range", NULL, 0);

	if(elf64)
		print_symbol_64(i);
	else
		print_symbol_32(i);
}
