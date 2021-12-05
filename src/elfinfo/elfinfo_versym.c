#include <bits/elf.h>
#include <string.h>
#include <util.h>

#include "elfinfo.h"

#define SYMVER_HIDDEN (1<<15)

typedef void (*scanfn)(void);

struct section {
	uint offset;
	uint size;
	uint link;
} versym, verdef, verneed, dynsym;

struct scanver {
	uint ndx;
	uint hash;
	uint file;
	uint name;
	uint done;

	scanfn pre;
	scanfn mid;
	scanfn post;
} scanver;

uint maxverndx;
uint searchndx;

static void call(scanfn ff)
{
	if(ff) ff();
}

static uint scan_veraux(uint off, void* ptr, void* end)
{
	struct elf_veraux* aux = ptr + off;

	if(ptr + sizeof(*aux) > end)
		fail("invalid veraux ref", NULL, 0);

	scanver.name = F.ldw(&aux->name);

	call(scanver.mid);

	uint next = F.ldw(&aux->next);

	return next ? off + next : next;
}

static uint scan_vernaux(uint off, void* ptr, void* end)
{
	struct elf_vernaux* aux = ptr + off;

	if(ptr + sizeof(*aux) > end)
		fail("invalid vernaux ref", NULL, 0);

	scanver.ndx = F.lds(&aux->other);
	scanver.hash = F.ldw(&aux->hash);
	scanver.name = F.ldw(&aux->name);

	if(!searchndx || (searchndx == scanver.ndx))
		call(scanver.mid);
	if(searchndx && (searchndx == scanver.ndx))
		scanver.done = 1;

	uint next = F.ldw(&aux->next);

	return next ? off + next : next;
}

static void scan_verdef(void)
{
	if(!verdef.offset)
		return;

	use_strings_from(verdef.link);

	void* ptr = ptrat(verdef.offset);
	void* end = ptr + verdef.size;

	scanver.file = 0;
	scanver.done = 0;

	while(ptr < end) {
		struct elf_verdef* vd = ptr;

		if(ptr + sizeof(*vd) > end)
			fail("truncated VERDEF section", NULL, 0);

		uint aux = F.ldw(&vd->aux);
		uint next = F.ldw(&vd->next);
		uint ver = F.lds(&vd->version);

		if(ver != 1)
			fail("unexpected VERDEF version", NULL, 0);

		scanver.ndx = F.lds(&vd->ndx);
		scanver.hash = F.ldw(&vd->hash);
		scanver.name = 0;

		if(!searchndx || (searchndx == scanver.ndx)) {
			call(scanver.pre);

			while(aux > 0)
				aux = scan_veraux(aux, ptr, end);

			call(scanver.post);
		}

		if(searchndx && (searchndx == scanver.ndx)) {
			scanver.done = 1;
			break;
		}

		if(!next)
			break;
		if(next < sizeof(*vd))
			fail("invalid verdefs next ref", NULL, 0);

		ptr += next;
	};
}

static void scan_verneed(void)
{
	if(!verneed.offset)
		return;

	use_strings_from(verneed.link);

	void* ptr = ptrat(verneed.offset);
	void* end = ptr + verneed.size;

	scanver.done = 0;

	while(ptr < end) {
		struct elf_verneed* vn = ptr;

		if(ptr + sizeof(*vn) > end)
			fail("truncated VERNEED section", NULL, 0);

		uint aux = F.ldw(&vn->aux);
		uint next = F.ldw(&vn->next);

		if(F.lds(&vn->version) != 1)
			fail("unexpected VERNEED version", NULL, 0);

		scanver.ndx = 0;
		scanver.file = F.ldw(&vn->file);
		scanver.name = 0;

		call(scanver.pre);

		while(aux > 0)
			aux = scan_vernaux(aux, ptr, end);

		call(scanver.post);

		if(!next || scanver.done)
			break;
		if(next < sizeof(*vn))
			fail("invalid VERNEED next ref", NULL, 0);

		ptr += next;
	};
}

static void print_line_end(void)
{
	output("\n", 1);
}

static void print_verdef_pre(void)
{
	uint ndx = scanver.ndx;
	uint max = maxverndx;
	uint hash = scanver.hash;

	print_idx(ndx, max);
	print(" ");
	print_hash(hash);
	print(":");
}

static void print_verdef_mid(void)
{
	print(" ");
	print_strq(scanver.name);
}

static void print_verdef_post(void)
{
	print_line_end();
}

static void print_verneed_pre(void)
{
	print("# ");
	print_strq(scanver.file);
	print_end();
}

static void print_verneed_mid(void)
{
	print_idx(scanver.ndx, 1);
	print(" ");
	print_hash(scanver.hash);
	print(": ");
	print_strn(scanver.name);
	print_end();
}

static uint symbol_version(uint i)
{
	uint16_t* vers = range(versym.offset, versym.size);
	uint num = versym.size / sizeof(*vers);

	if(i >= num)
		return 0;

	return F.lds(&vers[i]);
}

static void print_symver_lead(uint idx, uint num, uint ver)
{
	uint max = maxverndx;
	uint vid = (ver & ~SYMVER_HIDDEN);

	print_idx(idx, num);
	print(" ");

	if(!vid)
		print_pad(max + 1);
	else
		print_idx(vid, max);

	print(" ");
}

static void print_symbol_type(uint shndx, uint ver)
{
	char t;

	if(ver & SYMVER_HIDDEN)
		t = '*';
	else if(shndx == SHN_ABS)
		t = 'A';
	else if(shndx)
		t = 'D';
	else
		t = '-';

	print_char(t);
	print(" ");
}

static void print_symbol_name(uint noff)
{
	use_strings_from(dynsym.link);

	print_strn(noff);
}

static void print_sym_verdef(void)
{
	print(" ");
	print_strn(scanver.name);
}

static void print_sym_verneed(void)
{
	print(" ");
	print_strn(scanver.name);
	print(" ");
	print_strb(scanver.file);
}

static void print_version_name(uint ver, uint shndx)
{
	uint vid = ver & ~SYMVER_HIDDEN;

	if(vid < 2)
		return;

	searchndx = vid;

	if(shndx) {
		print(" -");

		scanver.pre = NULL;
		scanver.mid = print_sym_verdef;
		scanver.post = NULL;

		scan_verdef();
	} else {
		print(" =");

		scanver.pre = NULL;
		scanver.mid = print_sym_verneed;
		scanver.post = NULL;

		scan_verneed();
	}
}

static void list_symvers_64(void)
{
	struct elf64sym* syms = range(dynsym.offset, dynsym.size);
	uint i, n = dynsym.size / sizeof(*syms);

	for(i = 0; i < n; i++) {
		struct elf64sym* sym = &syms[i];
		uint noff = F.ldw(&sym->name);
		uint shndx = F.lds(&sym->shndx);
		uint ver = symbol_version(i);

		print_symver_lead(i, n, ver);
		print_symbol_type(shndx, ver);
		print_symbol_name(noff);
		print_version_name(ver, shndx);
		print_line_end();
	}
}

static void list_symvers_32(void)
{
	struct elf32sym* syms = range(dynsym.offset, dynsym.size);
	uint i, n = dynsym.size / sizeof(*syms);

	for(i = 0; i < n; i++) {
		struct elf32sym* sym = &syms[i];
		uint noff = F.ldw(&sym->name);
		uint shndx = F.lds(&sym->shndx);
		uint ver = symbol_version(i);

		print_symver_lead(i, n, ver);
		print_symbol_type(shndx, ver);
		print_symbol_name(noff);
		print_version_name(ver, shndx);
		print_line_end();
	}
}

static void check_max_verndx(void)
{
	uint ndx = scanver.ndx;
	uint max = maxverndx;

	if(max < ndx)
		maxverndx = ndx;
}

static void find_max_verndx(void)
{
	searchndx = 0;

	scanver.pre = check_max_verndx;
	scanver.mid = NULL;
	scanver.post = NULL;

	scan_verdef();

	scanver.pre = NULL;
	scanver.mid = check_max_verndx;
	scanver.post = NULL;

	scan_verneed();
}

static void locate_table(struct section* ss, uint type)
{
	if(elf64) {
		struct elf64shdr* sh = find_section_64(type);

		if(!sh) return;

		ss->offset = F.ldx(&sh->offset);
		ss->size = F.ldx(&sh->size);
		ss->link = F.ldw(&sh->link);
	} else {
		struct elf32shdr* sh = find_section_32(type);

		if(!sh) return;

		ss->offset = F.ldw(&sh->offset);
		ss->size = F.ldw(&sh->size);
		ss->link = F.ldw(&sh->link);
	}
}

void dump_versym_table(void)
{
	locate_table(&versym, SHT_GNU_versym);
	locate_table(&verdef, SHT_GNU_verdef);
	locate_table(&verneed, SHT_GNU_verneed);
	locate_table(&dynsym, SHT_DYNSYM);

	if(!versym.offset)
		fail("no VERSYM in this file", NULL, 0);
	if(!dynsym.offset)
		fail("no DYNSYM in this file", NULL, 0);

	find_max_verndx();

	if(elf64)
		list_symvers_64();
	else
		list_symvers_32();
}

void dump_verdef_table(void)
{
	locate_table(&verdef, SHT_GNU_verdef);
	locate_table(&verneed, SHT_GNU_verneed);

	find_max_verndx();

	scanver.pre = print_verdef_pre;
	scanver.mid = print_verdef_mid;
	scanver.post = print_verdef_post;

	scan_verdef();

	scanver.pre = print_verneed_pre;
	scanver.mid = print_verneed_mid;
	scanver.post = NULL;

	scan_verneed();
}
