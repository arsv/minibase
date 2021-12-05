#include <bits/types.h>

struct elf64shdr;
struct elf32shdr;

struct elfinf {
	void* buf;
	uint size;

	uint shoff;
	uint shnum;
	uint shstrndx;

	uint phoff;
	uint phnum;

	uint stroff;
	uint strlen;

	uint symoff;
	uint symlen;
	uint symnum;
	uint symstr;

	uint dynoff;
	uint dynlen;
};

struct pfuncs {
	uint (*lds)(uint16_t* addr);
	uint (*ldw)(uint32_t* addr);
	uint (*ldx)(uint64_t* addr);

	void (*sts)(uint16_t* addr, uint val);
	void (*stw)(uint32_t* addr, uint val);
	void (*stx)(uint64_t* addr, uint val);
};

extern uint elf64;
extern struct pfuncs F;
extern struct elfinf E;

void no_more_arguments(void);
int got_more_arguments(void);
char* shift(void);
uint shift_int(void);

void output(const char* buf, ulong len);
void outfmt(const char* s, const char* p);
void outstr(const char* str);

void* ptrat(uint off);
void* range(uint off, uint len);

struct elf32shdr* get_shent_32(uint i);
struct elf64shdr* get_shent_64(uint i);

struct elf64shdr* find_section_64(uint type);
struct elf32shdr* find_section_32(uint type);

struct elf64sym* get_sym_64(uint i);
struct elf32sym* get_sym_32(uint i);

void use_strings_from(uint shndx);
void use_strings_from_shstrtab(void);
void use_strings_from_symstr(void);
char* string(uint off);

void print(const char* str);
void print_end(void);

void print_int(uint v);
void print_hex(uint v);
void print_u64(uint64_t v);
void print_x64(uint64_t v);

void print_strn(uint off);
void print_strq(uint off);
void print_strx(uint off);
void print_strb(uint off);

void print_idx(uint n, uint m);
void print_pad(uint m);
void print_raw(const char* p, uint n);
void print_char(char c);
void print_hash(uint v);

void print_tag(char* tag);
void print_tag_dec32(char* tag, uint32_t* v);
void print_tag_dec64(char* tag, uint64_t* v);
void print_tag_hex32(char* tag, uint32_t* v);
void print_tag_hex64(char* tag, uint64_t* v);

void dump_general_info(void);

void dump_program_table(void);
void dump_single_phdr(void);

void dump_sections_table(void);
void dump_single_shdr(void);

void dump_dynamic_table(void);
void dump_dynamic_soname(void);
void dump_dynamic_libs(void);
void dump_program_interp(void);

void dump_symbol_table(void);
void dump_section_symbols(void);

void dump_all_strings(void);
void dump_strtab_section(void);

void dump_dynsym_table(void);

void dump_versym_table(void);
void dump_verdef_table(void);

void dump_segment_sections(void);
void dump_single_sym(void);
void dump_single_dsym(void);
