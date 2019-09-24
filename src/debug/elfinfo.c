#include <bits/types.h>
#include <bits/elf.h>
#include <sys/file.h>
#include <sys/mman.h>

#include <string.h>
#include <format.h>
#include <printf.h>
#include <util.h>
#include <main.h>

#include "elfinfo.h"

ERRTAG("elfinfo");

static char outbuf[2048];

static void init_output(CTX)
{
	ctx->bo.fd = STDOUT;
	ctx->bo.buf = outbuf;
	ctx->bo.ptr = 0;
	ctx->bo.len = sizeof(outbuf);
}

void output(CTX, const char* buf, ulong len)
{
	bufout(&ctx->bo, (char*)buf, len);
}

void outstr(CTX, const char* str)
{
	output(ctx, str, strlen(str));
}

static void fini_output(CTX)
{
	bufoutflush(&ctx->bo);
}

static void mmap_whole(CTX, char* name)
{
	int fd, ret;
	struct stat st;
	ulong max = ~0UL;

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

	ctx->fd = fd;
	ctx->buf = buf;
	ctx->len = size;
}

static void check_header(CTX)
{
	void* buf = ctx->buf;
	ulong len = ctx->len;
	struct elfhdr* hdr = buf;
	int elf64, size, bigendian;

	if(len < sizeof(*hdr) || memcmp(hdr->tag, "\x7F" "ELF", 4))
		fail("not an ELF file", NULL, 0);

	if(hdr->class == ELF_32) {
		elf64 = 0;
		size = sizeof(struct elf32hdr);
	} else if(hdr->class == ELF_64) {
		elf64 = 1;
		size = sizeof(struct elf64hdr);
	} else {
		fail("unknown ELF class", NULL, hdr->class);
	}

	if(mem_off_cmp(len, size) < 0)
		fail("file truncated", NULL, 0);

	if(hdr->version != 1)
		fail("invalid ELF version", NULL, hdr->version);

	if(hdr->data == ELF_LSB)
		bigendian = 0;
	else if(hdr->data == ELF_MSB)
		bigendian = 1;
	else
		fail("unknown ELF endianess", NULL, hdr->data);

	ctx->elf64 = elf64;
	ctx->bigendian = bigendian;
#ifdef BIGENDIAN
	ctx->elfxe = !bigendian;
#else
	ctx->elfxe = bigendian;
#endif
}

static void copy_header_data(CTX)
{
	void* eh = ctx->buf; /* ELF header */
	int elf64 = ctx->elf64;
	int elfxe = ctx->elfxe;

	load_u16(ctx->elftype, elfhdr, eh, type);
	load_u16(ctx->machine, elfhdr, eh, machine);
}

static void init_sections_table(CTX)
{
	void* eh = ctx->buf; /* ELF header */
	int elf64 = ctx->elf64;
	int elfxe = ctx->elfxe;

	load_x64(ctx->shoff, elfhdr, eh, shoff);
	load_u16(ctx->shnum, elfhdr, eh, shnum);
	load_u16(ctx->shentsize, elfhdr, eh, shentsize);
	load_u16(ctx->shstrndx,  elfhdr, eh, shstrndx);

	if(!ctx->shoff)
		return;
	if(ctx->shoff + ctx->shnum*ctx->shentsize <= ctx->len)
		return;

	ctx->shoff = 0;
	ctx->shnum = 0;

	warn("truncated sections table", NULL, 0);
}

static void init_program_table(CTX)
{
	void* eh = ctx->buf; /* ELF header */
	int elf64 = ctx->elf64;
	int elfxe = ctx->elfxe;

	load_x64(ctx->phoff,     elfhdr, eh, phoff);
	load_u16(ctx->phnum,     elfhdr, eh, phnum);
	load_u16(ctx->phentsize, elfhdr, eh, phentsize);
	load_x64(ctx->entry,     elfhdr, eh, entry);

	if(!ctx->phoff)
		return;
	if(ctx->phoff + ctx->phnum*ctx->phentsize <= ctx->len)
		return;

	ctx->phoff = 0;
	ctx->phnum = 0;

	warn("truncated program table", NULL, 0);
}

static void prep_file(CTX, char* name)
{
	mmap_whole(ctx, name);
	check_header(ctx);

	copy_header_data(ctx);
	init_sections_table(ctx);
	init_program_table(ctx);
}

static const struct cmd {
	char name[16];
	void (*call)(CTX);
} commands[] = {
	{ "sec",       dump_sections_table },
	{ "sections" , dump_sections_table },
	{ "sym",       dump_symbols        },
	{ "symbols",   dump_symbols        },
	{ "src",       dump_sources        },
	{ "sources",   dump_sources        },
	{ "seg",       dump_program_header },
	{ "program",   dump_program_header },
	{ "ld",        dump_program_interp },
	{ "interp",    dump_program_interp },
	{ "dyn",       dump_dynamic_info   },
	{ "dynamic",   dump_dynamic_info   },
	{ "so",        dump_dynamic_soname },
	{ "soname",    dump_dynamic_soname },
	{ "libs",      dump_dynamic_libs   },
	{ "needed",    dump_dynamic_libs   },
	{ "ss",        dump_sect_syms      },
	{ "sect-syms", dump_sect_syms      },
};

static void dispatch(CTX, char* cmd, char* name)
{
	const struct cmd* cc;

	for(cc = commands; cc < ARRAY_END(commands); cc++)
		if(!strncmp(cmd, cc->name, sizeof(cc->name)))
			break;
	if(cc >= ARRAY_END(commands))
		fail("unknown command", cmd, 0);

	prep_file(ctx, name);

	cc->call(ctx);
}

static void show_general_info(CTX, char* name)
{
	prep_file(ctx, name);

	dump_general_info(ctx);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	memzero(ctx, sizeof(*ctx));
	init_output(ctx);

	if(argc < 2)
		fail("too few arguments", NULL, 0);
	if(argc > 3)
		fail("too many arguments", NULL, 0);

	if(argc == 3)
		dispatch(ctx, argv[1], argv[2]);
	else
		show_general_info(ctx, argv[1]);

	fini_output(ctx);

	return 0;
}
