#include <sys/file.h>
#include <sys/mman.h>

#include <string.h>
#include <main.h>
#include <util.h>

#include "shead.h"

ERRTAG("shead");

struct source S;
struct output D;
struct pfuncs F;

uint elf64;

static void map_source(void)
{
	char* name = S.name;
	int fd, ret;
	struct stat st;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail("open", name, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", name, ret);
	if(st.size > 0xFFFFFFFF)
		fail("file too large", NULL, 0);

	uint size = st.size;
	int prot = PROT_READ;
	int flags = MAP_PRIVATE;

	void* buf = sys_mmap(NULL, size, prot, flags, fd, 0);

	if((ret = mmap_error(buf)))
		fail("mmap", NULL, ret);

	if((ret = sys_close(fd)) < 0)
		fail("close", name, ret);

	S.buf = buf;
	S.len = size;
	S.name = name;
}

static uint estimate_output_size(void)
{
	uint size = S.dynsym.size + S.dynsym.strlen;

	size += S.versym.size + S.verdef.size;

	return pagealign(size + 4*PAGE);
}

static void map_output(void)
{
	uint size = estimate_output_size();
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;

	int ret;
	void* buf = sys_mmap(NULL, size, prot, flags, -1, 0);

	if((ret = mmap_error(buf)))
		fail("mmap", NULL, ret);

	D.len = size;
	D.buf = buf;
	D.ptr = 0;
}

void* alloc_append(uint size)
{
	uint ptr = D.ptr;
	uint new = ptr + size;
	void* dst = D.buf + ptr;

	if(new >= D.len)
		fail("out of output space", NULL, 0);

	D.ptr = new;

	return dst;
}

void append_data(void* ptr, uint len)
{
	void* dst = alloc_append(len);

	memcpy(dst, ptr, len);
}

void append_zeroes(uint len)
{
	void* dst = alloc_append(len);

	memzero(dst, len);
}

void append_pad8(void)
{
	uint tail = D.ptr % 8;

	if(!tail) return;

	int i, need = 8 - tail;

	char* dst = alloc_append(need);

	for(i = 0; i < need; i++)
		dst[i] = 0;
}

void* srcptr(uint off)
{
	return S.buf + off;
}

void* dstptr(uint off)
{
	return D.buf + off;
}

static void write_output(void)
{
	char* name = D.name;
	int fd, ret;
	int flags = O_WRONLY | O_TRUNC | O_CREAT;
	int mode = 0755;

	void* buf = D.buf;
	uint len = D.ptr;

	if((fd = sys_open3(name, flags, mode)) < 0)
		fail("creat", name, fd);

	if((ret = sys_write(fd, buf, len)) < 0)
		fail("write", name, ret);

	if((ret = sys_close(fd)) < 0)
		fail("close", name, ret);
}

static void parse_arguments(int argc, char** argv)
{
	if(argc < 3)
		fail("too few arguments", NULL, 0);
	if(argc > 4)
		fail("too many arguments", NULL, 0);

	D.name = argv[1];
	S.name = argv[2];

	if(argc > 3)
		D.soname.str = argv[3];
	else
		D.soname.str = basename(S.name);
}

int main(int argc, char** argv)
{
	parse_arguments(argc, argv);

	map_source();
	analyze_source();

	map_output();
	compose_output();
	write_output();

	return 0;
}
