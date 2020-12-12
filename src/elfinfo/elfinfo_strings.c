#include <bits/elf.h>
#include <format.h>
#include <string.h>
#include <printf.h>
#include <util.h>
#include "elfinfo.h"

const char* lookup_string(CTX, uint off)
{
	uint64_t strings_off = ctx->strings_off;
	uint64_t strings_len = ctx->strings_len;

	if(!strings_off)
		return NULL;
	if(off > strings_len)
		return NULL;

	char* strings = ctx->buf + strings_off;
	char* s = strings + off;
	char* p = s;
	char* e = strings + strings_len;

	while(p < e && *p)
		p++;
	if(p >= e)
		return NULL;

	return s;
}

void use_strings_at_address(CTX, uint64_t off, uint64_t len)
{
	int elf64 = ctx->elf64;
	int elfxe = ctx->elfxe;

	uint64_t phoff = ctx->phoff;
	uint16_t phentsize = ctx->phentsize;
	int i, phnum = ctx->phnum;

	if(!phoff)
		return;

	for(i = 0; i < phnum; i++) {
		void* loc = ctx->buf + phoff + i*phentsize;

		uint32_t type;
		uint64_t vaddr, offset, filesz;

		take_u32(elfphdr, loc, type);
		take_x64(elfphdr, loc, vaddr);
		take_x64(elfphdr, loc, offset);
		take_x64(elfphdr, loc, filesz);

		if(type != PT_LOAD)
			continue;
		if(vaddr > off)
			break;
		if(vaddr + filesz < off + len)
			continue;

		ctx->strings_off = offset + (off - vaddr);
		ctx->strings_len = len;

		return;
	}

	ctx->strings_off = 0;
	ctx->strings_len = 0;
}

void use_strings_at_offset(CTX, uint64_t off, uint64_t len)
{
	ctx->strings_off = off;
	ctx->strings_len = len;
}

void reset_strings_location(CTX)
{
	ctx->strings_off = 0;
	ctx->strings_len = 0;
}
