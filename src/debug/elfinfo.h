#include <bits/types.h>
#include <output.h>
#include <endian.h>

#define SYMBOLS 1
#define SOURCES 2

struct top {
	int fd;
	void* buf;
	ulong len;

	struct bufout bo;

	int elf64;
	int elfxe;

	uint16_t elftype;
	uint16_t machine;
	int bigendian;

	uint64_t shoff;
	uint16_t shnum;
	uint16_t shentsize;
	uint16_t shstrndx;

	uint64_t phoff;
	uint16_t phnum;
	uint16_t phentsize;
	uint64_t entry;

	uint64_t strings_off;
	uint64_t strings_len;

	byte* sectmarks;

	uint count;
};

#define CTX struct top* ctx __unused

void output(CTX, const char* buf, ulong len);
void outstr(CTX, const char* str);
void dump_general_info(CTX);
void dump_program_header(CTX);
void dump_program_interp(CTX);
void dump_sections_table(CTX);
void dump_dynamic_info(CTX);
void dump_dynamic_soname(CTX);
void dump_dynamic_libs(CTX);
void dump_symbols(CTX);
void dump_sources(CTX);
void dump_sect_syms(CTX);

int got_any_dynamic_entries(CTX);

void locate_strings_section(CTX);
void reset_strings_location(CTX);
void use_strings_at_address(CTX, uint64_t off, uint64_t len);
void use_strings_at_offset(CTX, uint64_t off, uint64_t len);
const char* lookup_string(CTX, uint off);

/* The problem with parsing ELFs is that the files can be either ELF32
   or ELF64, and the byte order may or may not match that of the host.
   To keep the code somewhat palatable, we extend all ELF32 fields
   to match ELF64, and use macros so that the code has one-line loads
   equivalent to

       dst = ptr->field

   which gets expanded to if() covering both bitness and endianess.

   This was originally done with pointers, and had some protection against
   using say load_16 on a 32-bit long field, but GCC did not like that so
   it got rewritten to use assignements. Stuff like

       load_32(foo, elfstruct, ptr, field)

   will result in invalid data unless both foo and field are uint32-s. */

static inline uint16_t swab_u16(int xe, uint16_t src)
{
	return xe ? swabs(src) : src;
}

static inline uint32_t swab_u32(int xe, uint32_t src)
{
	return xe ? swabl(src) : src;
}

static inline uint64_t ext_long(int xe, uint32_t src)
{
	return (uint64_t)(xe ? swabl(src) : src);
}


static inline uint64_t swab_x64(int xe, uint64_t src)
{
	return xe ? swabx(src) : src;
}

/* Renames so that we can use typy##32 and type##64 below uniformly */

#define elfhdr32  struct elf32hdr*
#define elfhdr64  struct elf64hdr*
#define elfshdr32 struct elf32shdr*
#define elfshdr64 struct elf64shdr*
#define elfphdr32 struct elf32phdr*
#define elfphdr64 struct elf64phdr*
#define elfdyn32 struct elf32dyn*
#define elfdyn64 struct elf64dyn*
#define elfsym32 struct elf32sym*
#define elfsym64 struct elf64sym*

/* load(dst, type, ptr, field) means dst = ((type)ptr)->field */

#define load_u8(dst, type, ptr, field) { \
	if(elf64) \
		dst = ((type##64)ptr)->field; \
	else \
		dst = ((type##32)ptr)->field; \
}

#define load_u16(dst, type, ptr, field) { \
	if(elf64) \
		dst = swab_u16(elfxe, ((type##64)ptr)->field); \
	else \
		dst = swab_u16(elfxe, ((type##32)ptr)->field); \
}

#define load_u32(dst, type, ptr, field) { \
	if(elf64) \
		dst = swab_u32(elfxe, ((type##64)ptr)->field); \
	else \
		dst = swab_u32(elfxe, ((type##32)ptr)->field); \
}

#define load_x64(dst, type, ptr, field) { \
	if(elf64) \
		dst = swab_x64(elfxe, ((type##64)ptr)->field); \
	else \
		dst = ext_long(elfxe, ((type##32)ptr)->field); \
}

/* take(type, ptr, field) means field = ((type)ptr)->field

   This is used to extract struct fields into local variables with
   the same exact name. */

#define take_u16(type, ptr, field) load_u16(field, type, ptr, field)
#define take_u32(type, ptr, field) load_u32(field, type, ptr, field)
#define take_x64(type, ptr, field) load_x64(field, type, ptr, field)
