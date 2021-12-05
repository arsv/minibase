#include <bits/elf.h>

#include <string.h>
#include <util.h>

#include "elfinfo.h"

static const struct machine {
	int key;
	char* name;
} machines[] = {
	{ 62,  "x86-64",    },
	{ 3,   "x86",       },
	{ 40,  "ARM",       },
	{ 183, "ARM64",     },
	{ 20,  "PowerPC",   },
	{ 21,  "PPC64",     },
	{ 2,   "SPARC",     },
	{ 8,   "MIPS",      },
	{ 83,  "AVR",       },
	{ 92,  "OpenRISC",  },
	{ 189, "MicroBlaze",},
	{ 93,  "ARC",       },
	{ 94,  "Xtensa",    },
	{ 106, "Blackfin",  },
	{ 224, "AMD GPU",   },
	{ 243, "RISC-V",    }
};

static const char* const types[] = {
	[0] = "null-type",
	[1] = "object file",
	[2] = "executable",
	[3] = "shared lib",
	[4] = "core dump"
};

static const struct osabi {
	short os;
	char* name;
} osabis[] = {
	{   0, "SYSV"       },
	{   1, "HPUX"       },
	{   2, "NETBSD"     },
	{   3, "GNU"        },
	{   6, "SOLARIS"    },
	{   7, "AIX"        },
	{   8, "IRIX"       },
	{   9, "FREEBSD"    },
	{  10, "TRU64"      },
	{  11, "MODESTO"    },
	{  12, "OPENBSD"    },
	{  64, "ARM_AEABI"  },
	{  97, "ARM"        },
	{ 255, "STANDALONE" },
};

static void print_machine(int machine)
{
	const struct machine* mm;

	for(mm = machines; mm < ARRAY_END(machines); mm++)
		if(mm->key == machine)
			return print(mm->name);

	print("machine ");
	print_hex(machine & 0xFF);
}

static void print_type(uint type)
{
	if(type < ARRAY_SIZE(types)) {
		print(types[type]);
	} else {
		print("type ");
		print_int(type);
	}
}

static void print_osabi(struct elfhdr* eh)
{
	const struct osabi* pp;

	for(pp = osabis; pp < ARRAY_END(osabis); pp++) {
		if(pp->os != eh->osabi)
			continue;

		print(pp->name);
		goto ver;
	}

	print("OS ");
	print_int(eh->osabi);
ver:
	if(eh->abiversion) {
		print(" ABI ");
		print_int(eh->abiversion);
	}
}

static void print_ends(struct elfhdr* eh)
{
	uint data = eh->data;

	if(data == ELF_MSB)
		print("BE");
	else
		print("LE");
}

static void print_count(uint n, char* word)
{
	print_int(n);
	print(" ");
	print(word);

	if(n != 1) print("s");
}

static int has_dynamic_section(void)
{
	void* ptr;

	if(elf64)
		ptr = find_section_64(SHT_DYNAMIC);
	else
		ptr = find_section_32(SHT_DYNAMIC);

	return !!ptr;
}

/* Relevant fields are the same between elf64 and elf32,
   so elf64 check is skipped and elf32hdr is used for both cases.

   Note hdr->phnum and hdr->shnum *are* elf64 dependent but
   E.phnum and E.shnum are not. */

void dump_general_info(void)
{
	no_more_arguments();

	struct elf32hdr* hdr = range(0, sizeof(*hdr));

	uint type = hdr->type;
	uint machine = hdr->machine;
	uint version = hdr->version;

	if(elf64)
		print("ELF64 ");
	else
		print("ELF32 ");

	print_ends(&hdr->ident);
	print(" ");
	print_type(type);
	print(", ");
	print_machine(machine);
	print(" ");
	print_osabi(&hdr->ident);

	if(version != 1) {
		print(" version ");
		print_int(version);
	}

	print(", ");
	print_count(E.shnum, "section");
	print(", ");
	print_count(E.phnum, "segment");

	if(has_dynamic_section())
		print(", dynamic");

	print_end();
}
