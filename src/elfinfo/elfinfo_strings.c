#include <bits/elf.h>
#include <string.h>
#include <util.h>

#include "elfinfo.h"

static void section_line(uint shndx)
{
	uint noff;

	if(elf64) {
		struct elf64shdr* sh = get_shent_64(shndx);

		noff = F.ldw(&sh->name);
	} else {
		struct elf32shdr* sh = get_shent_32(shndx);

		noff = F.ldw(&sh->name);
	}

	print("# section ");
	print_int(shndx);
	print(" ");
	print_strn(noff);
	print_end();
}

static void note_offset(uint off, uint size)
{
	print_idx(off, size);
	print(" ");
}

static void dump_strings(uint type, uint off, uint size)
{
	if(type != SHT_STRTAB)
		fail("non-STRTAB section", NULL, 0);

	char* p = range(off, size);
	char* e = p + size;
	char* s = p;
	uint len = 0;

	for(; p < e; p++) {
		if(!len) note_offset(p - s, size);

		char c = *p;

		if(!c) {
			print("\n");
			len = 0;
		} else {
			print_raw(p, 1);
			len++;
		}
	}
}

static void dump_single_section(uint shndx)
{
	uint type, off, size;

	if(elf64) {
		struct elf64shdr* sh = get_shent_64(shndx);

		type = F.ldw(&sh->type);
		off = F.ldx(&sh->offset);
		size = F.ldx(&sh->size);
	} else {
		struct elf64shdr* sh = get_shent_64(shndx);

		type = F.ldw(&sh->type);
		off = F.ldx(&sh->offset);
		size = F.ldx(&sh->size);
	}

	dump_strings(type, off, size);
}

static int is_string_section(uint shndx)
{
	if(elf64) {
		struct elf64shdr* sh = get_shent_64(shndx);

		return (F.ldw(&sh->type) == SHT_STRTAB);
	} else {
		struct elf32shdr* sh = get_shent_32(shndx);

		return (F.ldw(&sh->type) == SHT_STRTAB);
	}
}

static void scan_all_string_sections(void)
{
	uint i, n = E.shnum;

	for(i = 0; i < n; i++) {
		if(!is_string_section(i))
			continue;

		section_line(i);

		dump_single_section(i);

		print_end();
	}
}

void dump_strtab_section(void)
{
	uint shndx = shift_int();

	no_more_arguments();

	dump_single_section(shndx);
}

void dump_all_strings(void)
{
	no_more_arguments();

	use_strings_from_shstrtab();

	scan_all_string_sections();
}
