#include <sys/file.h>
#include <sys/mman.h>
#include <sys/module.h>

#include <errtag.h>
#include <string.h>
#include <util.h>

ERRTAG("insmod");
ERRLIST(NEACCES NEAGAIN NEBADF NEINVAL NENFILE NENODEV NENOMEM NEPERM
	NETXTBSY NEOVERFLOW NEBADMSG NEBUSY NEFAULT NENOKEY NEEXIST
	NENOEXEC);

static void* mmap_module(const char* name, unsigned long* len)
{
	int fd;
	long ret;
	struct stat st;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail("cannot open", name, fd);

	if((ret = sys_fstat(fd, &st)) < 0)
		fail("cannot stat", name, ret);

	void* ptr = sys_mmap(NULL, st.size, PROT_READ, MAP_SHARED, fd, 0);

	if(mmap_error(ptr))
		fail("cannot mmap", name, (long)ptr);

	*len = st.size;
	return ptr;
}

static int load_module(char* name, char* pars)
{
	char* base = basename(name);

	unsigned long len;
	void* mod = mmap_module(name, &len);

	long ret = sys_init_module(mod, len, pars);

	if(ret < 0)
		fail("kernel rejects", base, ret);

	return 0;
};

int main(int argc, char** argv)
{
	if(argc < 2)
		fail("module name required", NULL, 0);
	if(argc > 4)
		fail("too many arguments", NULL, 0);

	char* name = argv[1];
	char* pars = argc > 3 ? argv[3] : "";

	return load_module(name, pars);
}
