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

#define CTX struct top* ctx

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

static inline void copy_one_byte(uint8_t* src, uint8_t* dst)
{
	*dst = *src;
}

static inline void copy_swab_u16(int xe, uint16_t* src, uint16_t* dst)
{
	*dst = xe ? swabs(*src) : *src;
}

static inline void copy_swab_u32(int xe, uint32_t* src, uint32_t* dst)
{
	*dst = xe ? swabl(*src) : *src;
}

static inline void copy_swab_u64(int xe, uint64_t* src, uint64_t* dst)
{
	*dst = xe ? swabx(*src) : *src;
}

static inline void copy_ext_long(int xe, uint32_t* src, uint64_t* dst)
{
	*dst = (uint64_t)(xe ? swabl(*src) : *src);
}

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

#define copy_u8(type, ptr, fld, dst) \
	copy_one_byte(elf64 ? \
			&(((type##64)ptr)->fld) : \
			&(((type##32)ptr)->fld), dst)
#define copy_u16(type, ptr, fld, dst) \
	copy_swab_u16(elfxe, elf64 ? \
			&(((type##64)ptr)->fld) : \
			&(((type##32)ptr)->fld), dst)
#define copy_u32(type, ptr, fld, dst) \
	copy_swab_u32(elfxe, elf64 ? \
			&(((type##64)ptr)->fld) : \
			&(((type##32)ptr)->fld), dst)
#define copy_x64(type, ptr, fld, dst) { \
	if(elf64) \
		copy_swab_u64(elfxe, &(((type##64)ptr)->fld), dst);\
	else \
		copy_ext_long(elfxe, &(((type##32)ptr)->fld), dst);\
}

#define take_u16(type, ptr, field) copy_u16(type, ptr, field, &field)
#define take_u32(type, ptr, field) copy_u32(type, ptr, field, &field)
#define take_x64(type, ptr, field) copy_x64(type, ptr, field, &field)
