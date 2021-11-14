#include <bits/types.h>

#define NSTRINGS 10
#define NSYMSEC 16

struct source {
	char* name;

	void* buf;
	uint len;

	struct {
		uint phoff;
		uint phnum;
		uint shoff;
		uint shnum;
		uint shstrndx;
	} header;

	struct {
		uint offset;
		uint size;
		uint info;
		uint stroff;
		uint strlen;
	} dynsym;

	struct {
		uint offset;
		uint size;
	} versym;

	struct {
		uint offset;
		uint size;
		uint info;
		uint stroff;
		uint strlen;
	} verdef;
};

struct output {
	char* name;

	void* buf;
	uint len;
	uint ptr;

	struct {
		char* str;
		uint offset;
	} soname;

	struct {
		uint offset;
		uint count;
	} sections;

	struct {
		uint offset;
		uint count;

	} segments;

	struct {
		uint offset;
		uint count;
		uint size;
		uint info;
		uint entsize;
	} dynsym;

	struct {
		uint offset;
		uint size;
	} dynstr;

	struct {
		uint offset;
		uint size;
	} dynamic;

	struct {
		uint offset;
		uint size;
	} versym;

	struct {
		uint offset;
		uint size;
		uint info;
	} verdef;

	struct {
		uint offset;
		uint shndx;
		uint size;

		uint off[NSTRINGS];
		uint count;
	} shstrtab;

	struct {
		uint count;
		uint start;
		uint keys[NSYMSEC];
	} symsec;
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

extern struct source S;
extern struct output D;
extern struct pfuncs F;

void* srcptr(uint off);
void* dstptr(uint off);

void analyze_source(void);
void compose_output(void);

void* alloc_append(uint size);
void append_data(void* buf, uint len);
void append_zeroes(uint len);
void append_pad8(void);

void transfer_symbols(void);
void update_symbols(void);
