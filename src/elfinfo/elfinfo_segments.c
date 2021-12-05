#include <bits/elf.h>
#include <string.h>
#include <util.h>

#include "elfinfo.h"

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
	if(type == 0x6474e553)
		return "PROPERTY";

	return NULL;
}

static void print_type(uint type)
{
	const char* name;

	if((name = typename(type)))
		print(name);
	else
		print_hex(type);
}

static void print_pflags(uint flags)
{
	if(flags == PF_R)
		return print("ro");
	if(!flags)
		return print("na");

	if(flags & PF_R)
		print("r");
	if(flags & PF_W)
		print("w");
	if(flags & PF_X)
		print("x");
}

struct elf64phdr* get_phdr_64(uint i)
{
	if(i >= E.phnum)
		fail("invalid segment reference", NULL, 0);

	struct elf64phdr* ph = range(E.phoff, (i+1)*sizeof(*ph));

	return &ph[i];
}

struct elf32phdr* get_phdr_32(uint i)
{
	if(i >= E.phnum)
		fail("invalid segment reference", NULL, 0);

	struct elf32phdr* ph = range(E.phoff, (i+1)*sizeof(*ph));

	return &ph[i];
}

static void dump_segment_64(uint i)
{
	struct elf64phdr* ph = get_phdr_64(i);

	uint type = F.ldw(&ph->type);
	uint flags = F.ldw(&ph->flags);
	uint fsize = F.ldx(&ph->filesz);
	uint msize = F.ldx(&ph->memsz);
	uint off = F.ldx(&ph->offset);

	print_idx(i, E.phnum);
	print(" ");
	print_type(type);

	if(fsize) {
		print(" ");
		print_int(fsize);
		print(" from ");
		print_int(off);
	}

	if(msize != fsize) {
		print(" memsz ");
		print_int(msize);
	}

	print(" ");
	print_pflags(flags);

	print_end();
}

static void dump_segment_32(uint i)
{
	struct elf32phdr* ph = get_phdr_32(i);

	uint type = F.ldw(&ph->type);
	uint flags = F.ldw(&ph->flags);
	uint fsize = F.ldw(&ph->filesz);
	uint msize = F.ldw(&ph->memsz);
	uint off = F.ldw(&ph->offset);

	print_idx(i, E.phnum);
	print(" ");
	print_pflags(flags);
	print(" ");
	print_type(type);

	if(fsize) {
		print(" ");
		print_int(fsize);
		print(" from ");
		print_int(off);
	}

	if(msize != fsize) {
		print(" memsz ");
		print_int(msize);
	}

	print_end();
}

void dump_program_table(void)
{
	no_more_arguments();

	int i, n = E.phnum;

	if(!n) fail("empty program table", NULL, 0);

	for(i = 0; i < n; i++) {
		if(elf64)
			dump_segment_64(i);
		else
			dump_segment_32(i);
	}
}

static uint locate_interp(void)
{
	uint i, n = E.phnum;

	for(i = 0; i < n; i++) {
		if(elf64) {
			struct elf64phdr* ph = get_phdr_64(i);
			uint type = F.ldw(&ph->type);

			if(type == PT_INTERP)
				return i;
		} else {
			struct elf32phdr* ph = get_phdr_32(i);
			uint type = F.ldw(&ph->type);

			if(type == PT_INTERP)
				return i;
		}
	}

	fail("no INTERP in this file", NULL, 0);
}

void dump_program_interp(void)
{
	no_more_arguments();

	uint offset, size;
	uint i = locate_interp();

	if(elf64) {
		struct elf64phdr* ph = get_phdr_64(i);

		offset = F.ldx(&ph->offset);
		size = F.ldx(&ph->filesz);
	} else {
		struct elf32phdr* ph = get_phdr_32(i);

		offset = F.ldw(&ph->offset);
		size = F.ldw(&ph->filesz);
	}

	char* buf = range(offset, size);

	output(buf, size);
	outstr("\n");
}

static void print_phdr_type(uint32_t* v)
{
	uint type = F.ldw(v);
	const char* name;

	print_tag("type");
	print_hex(type);

	if((name = typename(type))) {
		print(" ");
		print(name);
	}

	print_end();
}

static void print_phdr_flags(uint* v)
{
	uint flags = F.ldw(v);

	print_tag("flags");
	print_hex(flags);

	if(flags & (PF_R | PF_W | PF_X))
		print(" ");
	if(flags & PF_R)
		print("R");
	if(flags & PF_W)
		print("W");
	if(flags & PF_X)
		print("X");

	print_end();
}

static void dump_single_phdr_64(uint phndx)
{
	struct elf64phdr* ph = get_phdr_64(phndx);

	print_phdr_type(&ph->type);
	print_phdr_flags(&ph->flags);

	print_tag_dec64("offset", &ph->offset);
	print_tag_hex64("vaddr", &ph->vaddr);
	print_tag_hex64("paddr", &ph->paddr);
	print_tag_dec64("filesz", &ph->filesz);
	print_tag_dec64("memsz", &ph->memsz);
	print_tag_dec64("align", &ph->align);
}

static void dump_single_phdr_32(uint phndx)
{
	struct elf32phdr* ph = get_phdr_32(phndx);

	print_phdr_type(&ph->type);
	print_phdr_flags(&ph->flags);

	print_tag_dec32("offset", &ph->offset);
	print_tag_hex32("vaddr", &ph->vaddr);
	print_tag_hex32("paddr", &ph->paddr);
	print_tag_dec32("filesz", &ph->filesz);
	print_tag_dec32("memsz", &ph->memsz);
	print_tag_dec32("align", &ph->align);
}

void dump_single_phdr(void)
{
	uint phndx = shift_int();

	no_more_arguments();

	if(elf64)
		dump_single_phdr_64(phndx);
	else
		dump_single_phdr_32(phndx);
}

static void print_segment_line(uint i, uint type, uint size)
{
	print_idx(i, E.phnum);
	print(" ");
	print_type(type);
	print(" ");
	print_int(size);
	print_end();
}

static void print_section_name(uint ndx, uint noff, uint size)
{
	print("  ");
	print_idx(ndx, E.shnum);
	print(" ");
	print_strn(noff);
	print(" ");
	print_int(size);
	print_end();
}

static void scan_sections_within(uint phoff, uint phsize)
{
	uint i, n = E.shnum;
	uint shoff, shsize, shnoff;
	uint phend = phoff + phsize;

	for(i = 0; i < n; i++) {
		if(elf64) {
			struct elf64shdr* sh = get_shent_64(i);

			shoff = F.ldx(&sh->offset);
			shsize = F.ldx(&sh->size);
			shnoff = F.ldw(&sh->name);
		} else {
			struct elf32shdr* sh = get_shent_32(i);

			shoff = F.ldw(&sh->offset);
			shsize = F.ldw(&sh->size);
			shnoff = F.ldw(&sh->name);
		}

		if(!shsize) continue;

		uint shend = shoff + shsize;

		if(shend <= phoff)
			continue;
		if(shoff >= phend)
			continue;

		print_section_name(i, shnoff, shsize);
	}
}

void dump_segment_sections(void)
{
	uint i, n = E.phnum;
	uint type, offset, size;

	if(!E.shnum)
		fail("no sections in this file", NULL, 0);
	if(!E.phnum)
		fail("no segments in this file", NULL, 0);

	use_strings_from_shstrtab();

	for(i = 0; i < n; i++) {
		if(elf64) {
			struct elf64phdr* ph = get_phdr_64(i);

			type = F.ldw(&ph->type);
			offset = F.ldx(&ph->offset);
			size = F.ldx(&ph->filesz);
		} else {
			struct elf32phdr* ph = get_phdr_32(i);

			type = F.ldw(&ph->type);
			offset = F.ldw(&ph->offset);
			size = F.ldw(&ph->filesz);
		}

		print_segment_line(i, type, size);
		scan_sections_within(offset, size);
	}
}
