#include <sys/creds.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/mmap.h>

#include <format.h>
#include <string.h>
#include <util.h>
#include <fail.h>

ERRTAG = "whoami";
ERRLIST = {
	REPORT(ENOSYS), REPORT(ENOENT), REPORT(ENOTDIR), REPORT(ENOMEM),
	RESTASNUMBERS
};

static char* mapfile(const char* name, int* size)
{
	long fd = xchk(sys_open(name, O_RDONLY), "cannot open", name);

	struct stat st;	
	xchk(sys_fstat(fd, &st), "cannot stat", name);	
	/* get larger-than-int files out of the picture */
	if(st.size > 0x7FFFFFFF)
		fail("file too large:", name, 0);

	const int prot = PROT_READ;
	const int flags = MAP_SHARED;
	long ret = sys_mmap(NULL, st.size, prot, flags, fd, 0);

	if(mmap_error(ret))
		fail("cannot mmap", name, ret);

	*size = st.size;
	return (char*)ret;
}

static char* findname(char* filedata, int len, char* uidstr, int* namelen)
{
	char* fileend = filedata + len;

	/* ns  ne        le */
	/* user:x:500:...\n */
	/*        is ie     */

	char *ls, *le;
	char *ns, *ne;
	char *is, *ie;
	for(ls = filedata; ls < fileend; ls = le + 1) {
		le = strecbrk(ls, fileend, '\n');

		ns = ls; ne = strecbrk(ls, le, ':');
		if(ne >= le) continue;

		is = strecbrk(ne + 1, le, ':') + 1;
		if(is >= le) continue;
		ie = strecbrk(is, le, ':');
		if(ie >= le) continue;

		if(strncmp(uidstr, is, ie - is))
			continue;

		*namelen = ne - ns;
		return ns;
	};

	return NULL;
}

int main(int argc, char** argv)
{
	if(argc > 1)
		fail("no options allowed", NULL, 0);

	int filesize;
	char* filedata = mapfile("/etc/passwd", &filesize);

	long uid = xchk(sys_geteuid(), NULL, NULL);
	char uidstr[20];
	char* uidend = uidstr + sizeof(uidstr) - 2;
	char* p = fmtlong(uidstr, uidend, uid); *p = '\0';

	int namelen;
	char* nameptr = findname(filedata, filesize, uidstr, &namelen);
	
	if(nameptr) {
		char name[namelen];
		memcpy(name, nameptr, namelen);
		name[namelen] = '\n';
		sys_write(1, name, namelen + 1);
	} else {
		*p = '\n';
		sys_write(1, uidstr, p - uidstr + 1);
	}

	return 0;
}
