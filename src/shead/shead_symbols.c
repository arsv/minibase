#include <bits/elf.h>

#include <string.h>
#include <util.h>

#include "shead.h"

static int special_section(uint shndx)
{
	return ((shndx >= 0xFF00) && (shndx <= 0xFFFF));
}

static uint section_typekey(uint type, uint flags)
{
	uint mask = SHF_WRITE | SHF_ALLOC | SHF_EXECINSTR | SHF_TLS;
	uint key = (flags & mask) << 1;

	if(type == SHT_PROGBITS)
		key |= 1;
	else if(type != SHT_NOBITS)
		return 0;

	return key;
}

static struct elf64shdr* get_shent_64(uint i)
{
	if(i > S.header.shnum)
		return NULL;

	struct elf64shdr* sh = srcptr(S.header.shoff);

	return &sh[i];
}

static struct elf32shdr* get_shent_32(uint i)
{
	if(i > S.header.shnum)
		return NULL;

	struct elf32shdr* sh = srcptr(S.header.shoff);

	return &sh[i];
}

static uint new_shndx_for(uint type, uint flags)
{
	uint key = section_typekey(type, flags);
	uint i, n = D.symsec.count;

	for(i = 0; i < n; i++)
		if(D.symsec.keys[i] == key)
			return D.symsec.start + i;

	return 0;
}

static uint map_section_64(uint shndx)
{
	if(special_section(shndx))
		return shndx;
	if(shndx >= S.header.shnum)
		return 0;

	struct elf64shdr* sh;

	if(!(sh = get_shent_64(shndx)))
		return 0;

	uint type = F.ldw(&sh->type);
	uint flags = F.ldx(&sh->flags);

	return new_shndx_for(type, flags);
}

static uint map_section_32(uint shndx)
{
	if(special_section(shndx))
		return shndx;
	if(shndx >= S.header.shnum)
		return 0;

	struct elf32shdr* sh;

	if(!(sh = get_shent_32(shndx)))
		return 0;

	uint type = F.ldw(&sh->type);
	uint flags = F.ldw(&sh->flags);

	return new_shndx_for(type, flags);
}

static void note_ref_section(uint type, uint flags)
{
	uint key = section_typekey(type, flags);
	uint i, n = D.symsec.count;

	if(key == 0)
		return;

	for(i = 0; i < n; i++)
		if(D.symsec.keys[i] == key)
			return;

	if(i >= NSYMSEC)
		fail("sym-sec table overflow", NULL, 0);

	D.symsec.keys[i] = key;
	D.symsec.count = n + 1;
}

static void note_symtype_64(uint shndx)
{
	struct elf64shdr* sh;

	if(!(sh = get_shent_64(shndx)))
		return;

	uint type = F.ldw(&sh->type);
	uint flags = F.ldx(&sh->flags);

	note_ref_section(type, flags);
}

static void note_symtype_32(uint shndx)
{
	struct elf32shdr* sh;

	if(!(sh = get_shent_32(shndx)))
		return;

	uint type = F.ldw(&sh->type);
	uint flags = F.ldw(&sh->flags);

	note_ref_section(type, flags);
}

static void append_symbol_64(uint i, struct elf64sym* sym)
{
	struct elf64sym* dst = alloc_append(sizeof(*dst));

	*dst = *sym;

	dst->value = i;

	F.stx(&dst->size, sym->size ? 8 : 0);
}

static void append_symbol_32(uint i, struct elf32sym* sym)
{
	struct elf32sym* dst = alloc_append(sizeof(*dst));

	*dst = *sym;

	dst->value = i;

	F.stw(&dst->size, sym->size ? 8 : 0);
}

static void copy_symbols_64(void)
{
	struct elf64sym* ss = srcptr(S.dynsym.offset);

	uint n = S.dynsym.size / sizeof(*ss);
	uint i = S.dynsym.info;

	append_zeroes(sizeof(*ss));

	for(; i < n; i++) {
		struct elf64sym* sym = &ss[i];

		uint shndx = sym->shndx;

		if(!shndx) continue;

		note_symtype_64(shndx);

		append_symbol_64(i, sym);
	}

	D.dynsym.entsize = sizeof(struct elf64sym);
}

static void copy_symbols_32(void)
{
	struct elf32sym* ss = srcptr(S.dynsym.offset);

	uint n = S.dynsym.size / sizeof(*ss);
	uint i = S.dynsym.info;

	append_zeroes(sizeof(*ss));

	for(; i < n; i++) {
		struct elf32sym* sym = &ss[i];
		uint shndx = sym->shndx;

		if(!shndx) continue;

		note_symtype_32(shndx);

		append_symbol_32(i, sym);
	}

	D.dynsym.entsize = sizeof(struct elf32sym);
}

static void update_symbols_64(void)
{
	struct elf64sym* ss = dstptr(D.dynsym.offset);
	uint i, n = D.dynsym.size / sizeof(*ss);

	for(i = 1; i < n; i++) {
		struct elf64sym* sym = &ss[i];

		sym->value = 0;

		uint shndx = F.lds(&sym->shndx);
		uint shnew = map_section_64(shndx);

		F.sts(&sym->shndx, shnew);
	}
}

static void update_symbols_32(void)
{
	struct elf32sym* ss = dstptr(D.dynsym.offset);
	uint i, n = D.dynsym.size / sizeof(*ss);

	for(i = 1; i < n; i++) {
		struct elf32sym* sym = &ss[i];

		sym->value = 0;

		uint shndx = F.lds(&sym->shndx);
		uint shnew = map_section_32(shndx);

		F.sts(&sym->shndx, shnew);
	}
}

static void append_soname(void)
{
	char* soname = D.soname.str;

	if(!soname) return;

	uint len = strlen(soname) + 1;

	D.soname.offset = D.ptr - D.dynstr.offset;

	char* dst = alloc_append(len);

	memcpy(dst, soname, len);
}

static void copy_symbols(void)
{
	D.dynsym.offset = D.ptr;
	D.dynsym.count = 1;
	D.dynsym.info = 1;

	if(elf64)
		copy_symbols_64();
	else
		copy_symbols_32();

	D.dynsym.size = D.ptr - D.dynsym.offset;

	append_pad8();
}

static void append_symver(uint srcidx)
{
	ushort* vers = srcptr(S.versym.offset);
	uint count = S.versym.size / sizeof(*vers);

	uint vidx = (srcidx < count) ? vers[srcidx] : 0;

	ushort* dst = alloc_append(sizeof(*dst));

	F.sts(dst, vidx);
}

static void append_symvers_64(void)
{
	struct elf64sym* ss = dstptr(D.dynsym.offset);
	uint i, n = D.dynsym.size / sizeof(*ss);

	for(i = 0; i < n; i++) {
		struct elf64sym* sym = &ss[i];
		uint idx = sym->value;

		append_symver(idx);
	}
}

static void append_symvers_32(void)
{
	struct elf32sym* ss = dstptr(D.dynsym.offset);
	uint i, n = D.dynsym.size / sizeof(*ss);

	for(i = 0; i < n; i++) {
		struct elf32sym* sym = &ss[i];
		uint idx = sym->value;

		append_symver(idx);
	}
}

static void copy_symvers(void)
{
	if(!S.versym.offset) return;

	D.versym.offset = D.ptr;

	if(elf64)
		append_symvers_64();
	else
		append_symvers_32();

	D.versym.size = D.ptr - D.versym.offset;

	append_pad8();
}

static void copy_verdefs(void)
{
	uint srcoff = S.verdef.offset;
	uint size = S.verdef.size;

	if(!srcoff) return;

	D.verdef.offset = D.ptr;
	D.verdef.size = size;
	D.verdef.info = S.verdef.info;

	void* src = srcptr(srcoff);
	void* dst = alloc_append(size);

	memcpy(dst, src, size);
}

static char* symbol_name(char* buf, uint off, uint size)
{
	if(off >= size)
		return NULL;

	char* str = buf + off;

	uint left = size - off;
	uint len = strnlen(str, left);

	if(len >= left) /* not terminated */
		return NULL;

	return str;
}

static uint copy_string(char* str)
{
	uint len = strlen(str);

	uint ret = D.ptr - D.dynstr.offset;
	char* dst = alloc_append(len + 1);

	memcpy(dst, str, len + 1);

	return ret;
}

static uint copy_symbol_name(uint off)
{
	char* buf = srcptr(S.dynsym.stroff);
	uint size = S.dynsym.strlen;
	char* str = symbol_name(buf, off, size);

	if(!str) return 0;

	return copy_string(str);
}

static uint match_symbol_name(char* na, uint off)
{
	char* buf = dstptr(D.dynstr.offset);
	char* nb = buf + off;

	return !strcmp(na, nb);
}

static uint find_symbol_64(char* name)
{
	struct elf64sym* ss = dstptr(D.dynsym.offset);
	uint i, n = D.dynsym.size / sizeof(*ss);

	for(i = 1; i < n; i++) {
		struct elf64sym* sym = &ss[i];
		uint dstoff = F.ldw(&sym->name);
		uint shndx = F.lds(&sym->shndx);

		if(!special_section(shndx))
			continue;
		if(match_symbol_name(name, dstoff))
			return dstoff;
	}

	return 0;
}

static uint find_symbol_32(char* name)
{
	struct elf32sym* ss = dstptr(D.dynsym.offset);
	uint i, n = D.dynsym.size / sizeof(*ss);

	for(i = 1; i < n; i++) {
		struct elf32sym* sym = &ss[i];
		uint dstoff = F.ldw(&sym->name);
		uint shndx = F.lds(&sym->shndx);

		if(!special_section(shndx))
			continue;
		if(match_symbol_name(name, dstoff))
			return dstoff;
	}

	return 0;
}

static uint find_matching_symbol(char* name)
{
	if(!name)
		return 0;

	if(elf64)
		return find_symbol_64(name);
	else
		return find_symbol_32(name);
}

static uint copy_version_name(uint off)
{
	char* buf = srcptr(S.verdef.stroff);
	uint size = S.verdef.strlen;
	char* str = symbol_name(buf, off, size);
	uint ret;

	if((ret = find_matching_symbol(str)))
		return ret;

	return copy_string(str);
}

static void rename_symbols_64(void)
{
	struct elf64sym* ss = dstptr(D.dynsym.offset);
	uint i, n = D.dynsym.size / sizeof(*ss);

	for(i = 1; i < n; i++) {
		struct elf64sym* sym = &ss[i];
		uint srcoff = F.ldw(&sym->name);

		if(!srcoff) continue;

		uint dstoff = copy_symbol_name(srcoff);

		F.stw(&sym->name, dstoff);
	}
}

static void rename_symbols_32(void)
{
	struct elf32sym* ss = dstptr(D.dynsym.offset);
	uint i, n = D.dynsym.size / sizeof(*ss);

	for(i = 1; i < n; i++) {
		struct elf32sym* sym = &ss[i];
		uint srcoff = F.ldw(&sym->name);

		if(!srcoff) continue;

		uint dstoff = copy_symbol_name(srcoff);

		F.stw(&sym->name, dstoff);
	}
}

static uint walk_veraux(uint off, void* ptr, void* end)
{
	struct elf_veraux* aux = ptr + off;

	if(ptr + sizeof(*aux) > end)
		fail("invalid veraux ref", NULL, 0);

	uint srcoff = F.ldw(&aux->name);
	uint newoff = copy_version_name(srcoff);

	F.stw(&aux->name, newoff);

	uint next = F.ldw(&aux->next);

	return next ? off + next : next;
}

static void rename_verdefs(void)
{
	void* ptr = dstptr(D.verdef.offset);
	void* end = ptr + D.verdef.size;

	while(ptr < end) {
		struct elf_verdef* vd = ptr;

		if(ptr + sizeof(*vd) > end)
			fail("truncated verdefs section", NULL, 0);

		uint aux = F.ldw(&vd->aux);
		uint next = F.ldw(&vd->next);
		uint ver = F.lds(&vd->version);

		if(ver != 1)
			fail("unexpected verdef version", NULL, 0);

		while(aux > 0)
			aux = walk_veraux(aux, ptr, end);

		if(!next)
			break;
		if(next < sizeof(*vd))
			fail("invalid verdefs next ref", NULL, 0);

		ptr += next;
	};
}

static void copy_strings(void)
{
	D.dynstr.offset = D.ptr;

	append_zeroes(1);

	if(elf64)
		rename_symbols_64();
	else
		rename_symbols_32();

	rename_verdefs();

	append_soname();

	D.dynstr.size = D.ptr - D.dynstr.offset;

	append_pad8();
}

void transfer_symbols(void)
{
	copy_symbols();

	copy_symvers();
	copy_verdefs();

	copy_strings();
}

void update_symbols(void)
{
	if(elf64)
		update_symbols_64();
	else
		update_symbols_32();
}
