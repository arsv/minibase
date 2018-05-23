#include <bits/auxvec.h>

#include <main.h>
#include <output.h>
#include <format.h>
#include <util.h>

char* decimal(char* p, char* e, long n)
{
	return fmtlong(p, e, n);
}

char* pointer(char* p, char* e, long n)
{
	p = fmtstr(p, e, "0x");
	p = fmtxlong(p, e, n);
	return p;
}

char* hexnum(char* p, char* e, long n)
{
	p = fmtstr(p, e, "0x");
	p = fmtxlong(p, e, n);
	return p;
}

char* string(char* p, char* e, long n)
{
	return fmtstr(p, e, (char*) n);
}

char* random(char* p, char* e, long n)
{
	uint8_t* b = (uint8_t*) n;
	int i;

	for(i = 0; i < 16; i++) {
		if(i) p = fmtstr(p, e, " ");
		p = fmtpad0(p, e, 2, fmtxlong(p, e, b[i]));
	}

	return p;
}

const struct entry {
	long key;
	char* name;
	char* (*fmt)(char* p, char* e, long arg);
} entries[] = {
	{  2, "EXECFD",   decimal },
	{  3, "PHDR",     pointer },
	{  4, "PHENT",    decimal },
	{  5, "PHNUM",    decimal },
	{  6, "PAGESZ",   decimal },
	{  7, "BASE",     pointer },
	{  8, "FLAGS",    hexnum },
	{  9, "ENTRY",    pointer },
	{ 10, "NOTELF",   decimal },
	{ 11, "UID",      decimal },
	{ 12, "EUID",     decimal },
	{ 13, "GID",      decimal },
	{ 14, "EGID",     decimal },
	{ 15, "PLATFORM", string },
	{ 16, "HWCAP",    hexnum },
	{ 17, "CLKTCK",   decimal },
	{ 23, "SECURE",   decimal },
	{ 24, "BASE_PLATFORM", string },
	{ 25, "RANDOM",   random },
	{ 26, "HWCAP2",   hexnum },
	{ 31, "EXECFN",   string },
	{ 33, "EHDR",     pointer },
	{  0, NULL, NULL }
};

char outbuf[4096];

struct auxvec* skipenvp(char** envp)
{
	char** q = envp;

	while(*q) q++;

	return (struct auxvec*)(q + 1);
}

int main(int argc, char** argv)
{
	char** envp = argv + argc + 1;
	struct auxvec* a = skipenvp(envp);
	const struct entry* x;

	char* p = outbuf;
	char* e = outbuf + sizeof(outbuf) - 1;

	for(; a->key; a++) {
		for(x = entries; x->key; x++)
			if(x->key == a->key)
				break;

		if(x->key) {
			p = fmtstr(p, e, x->name);
			p = fmtstr(p, e, " ");
			p = x->fmt(p, e, a->val);
		} else {
			p = fmtlong(p, e, a->key);
			p = fmtstr(p, e, " ");
			p = decimal(p, e, a->val);
		}

		p = fmtstr(p, e, "\n");
	}

	writeall(STDOUT, outbuf, p - outbuf);

	return 0;
}
