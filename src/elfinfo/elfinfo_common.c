#include <bits/elf.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "elfinfo.h"

char fmtbuf[64];

void* ptrat(uint off)
{
	if(off >= E.size)
		fail("reference beyond EOF", NULL, 0);

	return E.buf + off;
}

void* range(uint off, uint len)
{
	if(off >= E.size)
		fail("reference beyond EOF", NULL, 0);
	if(off + len > E.size)
		fail("truncated file", NULL, 0);

	return E.buf + off;
}

struct elf32shdr* get_shent_32(uint i)
{
	if(i >= E.shnum)
		fail("invalid section index", NULL, 0);

	struct elf32shdr* sh = ptrat(E.shoff);

	return &sh[i];
}

struct elf64shdr* get_shent_64(uint i)
{
	if(i >= E.shnum)
		fail("invalid section index", NULL, 0);

	struct elf64shdr* sh = ptrat(E.shoff);

	return &sh[i];
}

struct elf64shdr* find_section_64(uint type)
{
	uint i, n = E.shnum;

	for(i = 0; i < n; i++) {
		struct elf64shdr* sh = get_shent_64(i);

		if(type == F.ldw(&sh->type))
			return sh;
	}

	return NULL;
}

struct elf32shdr* find_section_32(uint type)
{
	uint i, n = E.shnum;

	for(i = 0; i < n; i++) {
		struct elf32shdr* sh = get_shent_32(i);

		if(type == F.ldw(&sh->type))
			return sh;
	}

	return NULL;
}

void use_strings_from(uint shndx)
{
	uint type;

	if(elf64) {
		struct elf64shdr* sh = get_shent_64(shndx);

		type = F.ldw(&sh->type);

		uint off = F.ldx(&sh->offset);
		uint size = F.ldx(&sh->size);

		E.stroff = off;
		E.strlen = size;
	} else {
		struct elf32shdr* sh = get_shent_32(shndx);

		type = F.ldw(&sh->type);

		uint off = F.ldw(&sh->offset);
		uint size = F.ldw(&sh->size);

		E.stroff = off;
		E.strlen = size;
	}

	if(type == SHT_STRTAB)
		return;

	E.stroff = 0;
	E.strlen = 0;

	fail("strings in non-STRTAB section", NULL, 0);
}

void use_strings_from_shstrtab(void)
{
	use_strings_from(E.shstrndx);
}

void use_strings_from_symstr(void)
{
	use_strings_from(E.symstr);
}

struct elf64sym* get_sym_64(uint i)
{
	struct elf64sym* ss = range(E.symoff, E.symlen);

	return &ss[i];
}

struct elf32sym* get_sym_32(uint i)
{
	struct elf32sym* ss = range(E.symoff, E.symlen);

	return &ss[i];
}

char* string(uint off)
{
	uint size = E.strlen;

	if(off >= size)
		return NULL;

	char* buf = range(E.stroff, size);
	char* str = buf + off;

	uint left = size - off;
	uint len = strnlen(str, left);

	if(len >= left) /* not terminated */
		return NULL;

	return str;
}

char* fmt_string(char* p, char* e, uint noff)
{
	char* name = string(noff);

	if(noff && !name)
		return fmtstr(p, e, "(invalid)");
	if(!noff && (!name || !*name))
		return fmtstr(p, e, "(empty)");

	p = fmtstr(p, e, name);

	return p;
}

static uint count_digits_in(uint m)
{
	uint c = 1;

	while(m >= 10) { m /= 10; c++; }

	return c;
}

char* fmt_index(char* p, char* e, uint n, uint m)
{
	uint w = count_digits_in(m);

	return fmtpad0(p, e, w, fmtint(p, e, n));
}

char* fmt_space(char* p, char* e, uint m)
{
	uint w = count_digits_in(m);

	return fmtpad(p, e, w, p);
}

char* fmt_tag(char* p, char* e, char* tag)
{
	p = fmtstr(p, e, tag);
	p = fmtstr(p, e, ": ");
	return p;
}

char* fmt_nl(char* p, char* e)
{
	return fmtchar(p, e, '\n');
}

void print_tag(char* tag)
{
	print(tag);
	print(": ");
}

void print_tag_dec32(char* tag, uint32_t* v)
{
	FMTBUF(p, e, buf, 100);

	p = fmt_tag(p, e, tag);
	p = fmtuint(p, e, F.ldw(v));
	p = fmt_nl(p, e);

	output(buf, p - buf);
}

void print_tag_dec64(char* tag, uint64_t* v)
{
	FMTBUF(p, e, buf, 100);

	p = fmt_tag(p, e, tag);
	p = fmtu64(p, e, F.ldx(v));
	p = fmt_nl(p, e);

	output(buf, p - buf);
}

void print_tag_hex32(char* tag, uint32_t* v)
{
	FMTBUF(p, e, buf, 100);

	p = fmt_tag(p, e, tag);
	p = fmtstr(p, e, "0x");
	p = fmthex(p, e, F.ldw(v));
	p = fmt_nl(p, e);

	outfmt(buf, p);
}

void print_tag_hex64(char* tag, uint64_t* v)
{
	FMTBUF(p, e, buf, 100);

	p = fmt_tag(p, e, tag);
	p = fmtstr(p, e, "0x");
	p = fmtx64(p, e, F.ldx(v));
	p = fmt_nl(p, e);

	outfmt(buf, p);
}

void print(const char* str)
{
	outstr(str);
}

void print_int(uint v)
{
	char* s = fmtbuf;
	char* p = s;
	char* e = s + sizeof(fmtbuf);

	p = fmtint(p, e, v);

	outfmt(s, p);
}

void print_hex(uint v)
{
	char* s = fmtbuf;
	char* p = s;
	char* e = s + sizeof(fmtbuf);

	p = fmtstr(p, e, "0x");
	p = fmthex(p, e, v);

	outfmt(s, p);
}

void print_u64(uint64_t v)
{
	char* s = fmtbuf;
	char* p = s;
	char* e = s + sizeof(fmtbuf);

	p = fmtu64(p, e, v);

	outfmt(s, p);
}

void print_x64(uint64_t v)
{
	char* s = fmtbuf;
	char* p = s;
	char* e = s + sizeof(fmtbuf);

	p = fmtstr(p, e, "0x");
	p = fmtx64(p, e, v);

	outfmt(s, p);
}

void print_idx(uint n, uint m)
{
	char* s = fmtbuf;
	char* p = s;
	char* e = s + sizeof(fmtbuf);

	p = fmt_index(p, e, n, m);

	outfmt(s, p);
}

void print_pad(uint m)
{
	uint w = count_digits_in(m);

	while(w--) output(" ", 1);
}

void print_raw(const char* p, uint n)
{
	output(p, n);
}

void print_char(char c)
{
	char s[1];

	s[0] = c;

	output(s, 1);
}

void print_hash(uint v)
{
	char* s = fmtbuf;
	char* p = s;
	char* e = s + sizeof(fmtbuf);

	p = fmtpad0(p, e, 8, fmthex(p, e, v));

	outfmt(s, p);
}

void print_strn(uint off)
{
	char* str = string(off);

	if(!str)
		print("(invalid)");
	else if(!*str)
		print("(empty)");
	else
		outstr(str);
}

void print_strq(uint off)
{
	char* str = string(off);

	if(!str) {
		print("(invalid)");
	} else {
		print("\"");
		outstr(str);
		print("\"");
	}
}

void print_strb(uint off)
{
	char* str = string(off);

	if(!str) {
		print("(invalid)");
	} else {
		print("[");
		outstr(str);
		print("]");
	}
}

void print_strx(uint off)
{
	print_int(off);
	print(" ");
	print_strq(off);
}

void print_end(void)
{
	output("\n", 1);
}
