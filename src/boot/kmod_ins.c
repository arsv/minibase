#include <sys/module.h>

#include <string.h>
#include <util.h>

#include "kmod.h"

static int check_strip_suffix(char* name, int nlen, char* suffix)
{
	int slen = strlen(suffix);

	if(nlen < slen)
		return 0;
	if(strncmp(name + nlen - slen, suffix, slen))
		return 0;

	name[nlen-slen] = '\0';
	return 1;
}

void insmod(char* path, char* pars, char** envp)
{
	char* base = basename(path);
	int blen = strlen(base);
	char name[blen+1];

	memcpy(name, base, blen);
	name[blen] = '\0';

	struct mbuf mb;

	if(check_strip_suffix(name, blen, ".ko"))
		mmapwhole(&mb, path);
	else if(check_strip_suffix(name, blen, ".ko.gz"))
		decompress(&mb, path, "/bin/zcat", envp);
	else if(check_strip_suffix(name, blen, ".ko.xz"))
		decompress(&mb, path, "/bin/xzcat", envp);
	else
		fail("not a module:", path, 0);

	long ret = sys_init_module(mb.buf, mb.len, pars);

	if(ret && ret != -EEXIST)
		fail("init_module", name, ret);

	munmapbuf(&mb);
};
