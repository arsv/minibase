#include <sys/geteuid.h>
#include <sys/fstat.h>
#include <sys/open.h>
#include <sys/mmap.h>
#include <sys/write.h>

#include <fail.h>
#include <memcpy.h>
#include <fmtlong.h>
#include <strecbrk.h>
#include <strncmp.h>

ERRTAG = "whoami";
ERRLIST = {
	REPORT(ENOSYS), REPORT(ENOENT), REPORT(ENOTDIR), REPORT(ENOMEM),
	RESTASNUMBERS
};

static char* mapfile(const char* name, int* size)
{
	long fd = xchk(sysopen(name, O_RDONLY), "cannot open", name);

	struct stat st;	
	xchk(sysfstat(fd, &st), "cannot stat", name);	
	/* get larger-than-int files out of the picture */
	if(st.st_size > 0x7FFFFFFF)
		fail("file too large:", name, 0);

	const int prot = PROT_READ;
	const int flags = MAP_SHARED;
	long ret = sysmmap(NULL, st.st_size, prot, flags, fd, 0);

	if(MMAPERROR(ret))
		fail("cannot mmap", name, ret);

	*size = st.st_size;
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

	long uid = xchk(sysgeteuid(), NULL, NULL);
	char uidstr[20];
	char* uidend = uidstr + sizeof(uidstr) - 2;
	char* p = fmtlong(uidstr, uidend, uid); *p = '\0';

	int namelen;
	char* nameptr = findname(filedata, filesize, uidstr, &namelen);
	
	if(nameptr) {
		char name[namelen];
		memcpy(name, nameptr, namelen);
		name[namelen] = '\n';
		syswrite(1, name, namelen + 1);
	} else {
		*p = '\n';
		syswrite(1, uidstr, p - uidstr + 1);
	}

	return 0;
}
