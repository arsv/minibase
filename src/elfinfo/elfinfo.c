#include <bits/types.h>
#include <bits/elf.h>
#include <sys/file.h>
#include <sys/mman.h>

#include <string.h>
#include <format.h>
#include <memoff.h>
#include <output.h>
#include <endian.h>
#include <util.h>
#include <main.h>

#include "elfinfo.h"

#ifdef BIGENDIAN
#define NATIVE ELF_MSB
#else
#define NATIVE ELF_LSB
#endif

ERRTAG("elfinfo");

static char outbuf[2048];
static struct bufout bo;

uint elf64;
struct pfuncs F;
struct elfinf E;

struct args {
	int argc;
	int argi;
	char** argv;
} A;

char* shift(void)
{
	if(A.argi >= A.argc)
		fail("too few arguments", NULL, 0);

	return A.argv[A.argi++];
}

uint shift_int(void)
{
	char* p = shift();
	uint ret;

	if(!(p = parseuint(p, &ret)) || *p)
		fail("integer argument required", NULL, 0);

	return ret;
}

int got_more_arguments(void)
{
	return (A.argi < A.argc);
}

void no_more_arguments(void)
{
	if(got_more_arguments())
		fail("too many arguments", NULL, 0);
}


void output(const char* buf, ulong len)
{
	bufout(&bo, buf, len);
}

void outfmt(const char* s, const char* p)
{
	bufout(&bo, s, p - s);
}

void outstr(const char* str)
{
	output(str, strlen(str));
}

static uint lds_ne(uint16_t* addr) { return *addr; }
static uint ldw_ne(uint32_t* addr) { return *addr; }
static uint ldx_ne(uint64_t* addr) { return *addr; }

static uint lds_xe(uint16_t* addr) { return swabs(*addr); }
static uint ldw_xe(uint32_t* addr) { return swabl(*addr); }
static uint ldx_xe(uint64_t* addr) { return swabx(*addr); }

static void sts_ne(uint16_t* addr, uint val) { *addr = val; }
static void stw_ne(uint32_t* addr, uint val) { *addr = val; }
static void stx_ne(uint64_t* addr, uint val) { *addr = val; }

static void sts_xe(uint16_t* addr, uint val) { *addr = swabs(val); }
static void stw_xe(uint32_t* addr, uint val) { *addr = swabl(val); }
static void stx_xe(uint64_t* addr, uint val) { *addr = swabx(val); }

static void setup_native_endian(void)
{
	F.lds = lds_ne;
	F.ldw = ldw_ne;
	F.ldx = ldx_ne;

	F.sts = sts_ne;
	F.stw = stw_ne;
	F.stx = stx_ne;
}

static void setup_cross_endian(void)
{
	F.lds = lds_xe;
	F.ldw = ldw_xe;
	F.ldx = ldx_xe;

	F.sts = sts_xe;
	F.stw = stw_xe;
	F.stx = stx_xe;
}

static void check_header(void)
{
	void* buf = E.buf;
	ulong size = E.size;
	struct elfhdr* hdr = buf;

	if(size < sizeof(*hdr))
		fail("not an ELF file", NULL, 0);
	if(memcmp(hdr->tag, ELFTAG, 4))
		fail("not an ELF file", NULL, 0);
	if(hdr->version != 1)
		fail("unsupported ELF version", NULL, 0);

	byte bit = hdr->class;
	byte end = hdr->data;

	if(bit == ELF_64)
		elf64 = 1;
	else if(bit != ELF_32)
		fail("unexpected ELF hdr class", NULL, bit);

	if((end != ELF_LSB) && (end != ELF_MSB))
		fail("unexpected ELF hdr data", NULL, end);

	if(end == NATIVE)
		setup_native_endian();
	else
		setup_cross_endian();
}

static void read_header_64(void)
{
	struct elf64hdr* hdr = range(0, sizeof(*hdr));

	E.shnum = F.lds(&hdr->shnum);
	E.phnum = F.lds(&hdr->phnum);

	E.shoff = F.ldx(&hdr->shoff);
	E.phoff = F.ldx(&hdr->phoff);

	E.shstrndx = F.lds(&hdr->shstrndx);

	uint phent = F.lds(&hdr->phentsize);
	uint shent = F.lds(&hdr->shentsize);

	if((phent != sizeof(struct elf64phdr)) && E.phnum)
		fail("invalid phentsize", NULL, 0);
	if((shent != sizeof(struct elf64shdr)) && E.shnum)
		fail("invalid shentsize", NULL, 0);
}

static void read_header_32(void)
{
	struct elf32hdr* hdr = range(0, sizeof(*hdr));

	E.shnum = F.lds(&hdr->shnum);
	E.phnum = F.lds(&hdr->phnum);

	E.shoff = F.ldw(&hdr->shoff);
	E.phoff = F.ldw(&hdr->phoff);

	E.shstrndx = F.lds(&hdr->shstrndx);

	uint phent = F.lds(&hdr->phentsize);
	uint shent = F.lds(&hdr->shentsize);

	if((phent != sizeof(struct elf32phdr)) && E.phnum)
		fail("invalid phentsize", NULL, 0);
	if((shent != sizeof(struct elf32shdr)) && E.shnum)
		fail("invalid shentsize", NULL, 0);
}

static void mmap_whole(char* name)
{
	int fd, ret;
	struct stat st;
	ulong max = 0xFFFFFFFF;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", name, ret);

	if(mem_off_cmp(max, st.size) < 0)
		fail(NULL, name, -E2BIG);

	int prot = PROT_READ;
	int flags = MAP_SHARED;
	ulong size = st.size;

	void* buf = sys_mmap(NULL, size, prot, flags, fd, 0);

	if((ret = mmap_error(buf)))
		fail("mmap", name, ret);

	(void)fd;

	E.buf = buf;
	E.size = size;
}

static void prep_file(char* name)
{
	mmap_whole(name);
	check_header();

	if(elf64)
		read_header_64();
	else
		read_header_32();
}

static const struct cmd {
	char name[16];
	void (*call)(void);
} commands[] = {
	{ "ptable",     dump_program_table  },
	{ "phdr",       dump_single_phdr    },

	{ "stable",     dump_sections_table },
	{ "shdr",       dump_single_shdr    },

	{ "dynamic",   dump_dynamic_table  },
	{ "interp",    dump_program_interp },
	{ "soname",    dump_dynamic_soname },
	{ "needed",    dump_dynamic_libs   },

	{ "symtab",    dump_symbol_table   },
	{ "dynsym",    dump_dynsym_table   },
	{ "sym",       dump_single_sym     },
	{ "dsym",      dump_single_dsym    },

	{ "symsec",    dump_section_symbols },
	{ "secseg",    dump_segment_sections },
	{ "segsec",    dump_segment_sections },

	{ "strings",   dump_all_strings    },
	{ "strtab",    dump_strtab_section },

	{ "versym",    dump_versym_table   },
	{ "verdef",    dump_verdef_table   },
};

static void dispatch(char* cmd)
{
	const struct cmd* cc;

	for(cc = commands; cc < ARRAY_END(commands); cc++)
		if(!strncmp(cmd, cc->name, sizeof(cc->name)))
			break;
	if(cc >= ARRAY_END(commands))
		fail("unknown command", cmd, 0);

	cc->call();
}

int main(int argc, char** argv)
{
	A.argc = argc;
	A.argv = argv;
	A.argi = 1;

	bo.fd = STDOUT;
	bo.buf = outbuf;
	bo.ptr = 0;
	bo.len = sizeof(outbuf);

	prep_file(shift());

	if(got_more_arguments())
		dispatch(shift());
	else
		dump_general_info();

	bufoutflush(&bo);

	return 0;
}
