#include <endian.h>
#include <string.h>

#include "common.h"

/* The data that both modinfo and depmod need is located in .modinfo
   section of the module ELF file. The code below tries to figure out
   where exactly that section is within a mmaped decompressed image.

   Busybox skips the whole ELF parsing thing and just scans the image
   for "parm=", "description=" and such, risking false positives, but
   with elfinfo already written there's no point in tricks like that.

   See ../debug/elfinfo.h for origin of the ELF parsing code. */

#define MOD struct kmod* mod

#define elfhdr32  struct elf32hdr*
#define elfhdr64  struct elf64hdr*
#define elfshdr32 struct elf32shdr*
#define elfshdr64 struct elf64shdr*

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

static int moderr(CTX, const char* msg, MOD)
{
	return error(ctx, msg, mod->name, 0);
}

static int read_elf_header(CTX, MOD)
{
	void* buf = mod->buf;
	ulong len = mod->len;
	struct elfhdr* hdr = buf;
	int elf64, size, bigendian;

	if(len < sizeof(*hdr) || memcmp(hdr->tag, "\x7F" "ELF", 4))
		return moderr(ctx, "not an ELF file:", mod);

	if(hdr->class == ELF_32) {
		elf64 = 0;
		size = sizeof(struct elf32hdr);
	} else if(hdr->class == ELF_64) {
		elf64 = 1;
		size = sizeof(struct elf64hdr);
	} else {
		return moderr(ctx, "unknown ELF class:", mod);
	}

	if(mem_off_cmp(len, size) < 0)
		return moderr(ctx, "file truncated:", mod);

	if(hdr->version != 1)
		return moderr(ctx, "invalid ELF version:", mod);

	if(hdr->data == ELF_LSB)
		bigendian = 0;
	else if(hdr->data == ELF_MSB)
		bigendian = 1;
	else
		return moderr(ctx, "unknown ELF endianess:", mod);

	mod->elf64 = elf64;
#ifdef BIGENDIAN
	mod->elfxe = !bigendian;
#else
	mod->elfxe = bigendian;
#endif
	return 0;
}

const char* lookup_string(CTX, MOD, uint off)
{
	uint64_t strings_off = mod->strings_off;
	uint64_t strings_len = mod->strings_len;

	if(!strings_off)
		return NULL;
	if(off > strings_len)
		return NULL;

	char* strings = mod->buf + strings_off;
	char* s = strings + off;
	char* p = s;
	char* e = strings + strings_len;

	while(p < e && *p)
		p++;
	if(p >= e)
		return NULL;

	return s;
}

static int locate_strings_section(CTX, MOD)
{
	uint64_t shoff = mod->shoff;
	int shstrndx = mod->shstrndx;
	int shentsize = mod->shentsize;

	if(!shstrndx)
		return moderr(ctx, "no .strings in", mod);

	void* sh = mod->buf + shoff + shstrndx*shentsize;
	int elf64 = mod->elf64;
	int elfxe = mod->elfxe;

	uint32_t type;
	uint64_t offset, size;

	take_u32(elfshdr, sh, type);
	take_x64(elfshdr, sh, offset);
	take_x64(elfshdr, sh, size);

	if(type != SHT_STRTAB)
		return moderr(ctx, "invalid .strings section in", mod);

	mod->strings_off = offset;
	mod->strings_len = size;

	return 0;
}

static int locate_modinfo_section(CTX, MOD)
{
	uint64_t shnum = mod->shnum;
	uint64_t shoff = mod->shoff;
	uint16_t shentsize = mod->shentsize;
	int elf64 = mod->elf64;
	int elfxe = mod->elfxe;

	for(uint i = 0; i < shnum; i++) {
		void* ptr = mod->buf + shoff + i*shentsize;
		uint32_t name, type;
		uint64_t offset, size;
		const char* namestr;

		take_u32(elfshdr, ptr, name);
		take_u32(elfshdr, ptr, type);
		take_x64(elfshdr, ptr, offset);
		take_x64(elfshdr, ptr, size);

		if(!type)
			continue;
		if(!(namestr = lookup_string(ctx, mod, name)))
			continue;
		if(strcmp(namestr, ".modinfo"))
			continue;

		mod->modinfo_off = offset;
		mod->modinfo_len = size;

		return 0;
	}

	return moderr(ctx, "no .modinfo in", mod);
}

static int init_sections_table(CTX, MOD)
{
	uint len = mod->len;
	void* eh = mod->buf; /* ELF header */
	int elf64 = mod->elf64;
	int elfxe = mod->elfxe;

	uint64_t shoff;
	uint16_t shnum;
	uint16_t shentsize;
	uint16_t shstrndx;

	take_x64(elfhdr, eh, shoff);
	take_u16(elfhdr, eh, shnum);
	take_u16(elfhdr, eh, shentsize);
	take_u16(elfhdr, eh, shstrndx);

	if(!shoff)
		return moderr(ctx, "no sections in", mod);
	if(shoff + shnum*shentsize > len)
		return moderr(ctx, "corrupt sections header in", mod);

	mod->shoff = shoff;
	mod->shnum = shnum;
	mod->shentsize = shentsize;
	mod->shstrndx = shstrndx;

	return 0;
}

int find_modinfo(CTX, MOD, struct mbuf* mb, char* name)
{
	int ret;

	memzero(mod, sizeof(*mod));

	mod->name = name;
	mod->buf = mb->buf;
	mod->len = mb->len;

	if((ret = read_elf_header(ctx, mod)) < 0)
		return ret;
	if((ret = init_sections_table(ctx, mod)) < 0)
		return ret;
	if((ret = locate_strings_section(ctx, mod)) < 0)
		return ret;
	if((ret = locate_modinfo_section(ctx, mod)) < 0)
		return ret;

	return 0;
}
