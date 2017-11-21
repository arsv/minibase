#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/fprop.h>
#include <sys/creds.h>
#include <sys/mman.h>

#include <errtag.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "common.h"
#include "udevmod.h"

static int load_file(struct mbuf* mb, const char* name)
{
	int fd, ret;
	struct stat st;

	if(mb->buf)
		return 0;
	if((fd = sys_open(name, O_RDONLY)) < 0)
		return fd;
	if((ret = sys_fstat(fd, &st)) < 0)
		goto out;
	if(st.size > 1024*1024)
		goto out;

	void* buf;
	int prot = PROT_READ;
	int flags = MAP_PRIVATE;

	buf = sys_mmap(NULL, st.size, prot, flags, fd, 0);

	if((ret = mmap_error(buf)))
		goto out;

	mb->buf = buf;
	mb->len = st.size;
	ret = 0;
out:
	sys_close(fd);

	return ret;
}

static int mapid(struct mbuf* mb, const char* file, char* name)
{
	if(load_file(mb, file) < 0)
		return -1;

	char* filedata = mb->buf;
	char* fileend = filedata + mb->len;
	int id;

	/* user:x:500:...\n */
	/* ls  ue un     le */
	char *ls, *le;
	char *ue, *un;
	char *ne = NULL;
	for(ls = filedata; ls < fileend; ls = le + 1) {
		le = strecbrk(ls, fileend, '\n');
		ue = strecbrk(ls, le, ':');
		if(ue >= le) continue;
		un = strecbrk(ue + 1, le, ':') + 1;
		if(un >= le) continue;

		if(strncmp(name, ls, ue - ls))
			continue;
		if(!(ne = parseint(un, &id)) || *ne != ':')
			break;

		return id;
	};

	return -1;
}

static int resolve(char* p, char* e, struct mbuf* mb, const char* file)
{
	int val;
	char* q;

	if(p >= e)
		return -1; /* empty field */

	int len = (int)(e - p);
	char buf[len+2];

	memcpy(buf, p, len);
	buf[len] = '\0';

	if((q = parseint(p, &val)) && q >= e)
		return val;

	return mapid(mb, file, buf);
}

static int resolve_user(CTX, char* p, char* e)
{
	return resolve(p, e, &ctx->passwd, "/etc/passwd");
}

static int resolve_group(CTX, char* p, char* e)
{
	return resolve(p, e, &ctx->group, "/etc/group");
}

static void chown_node(CTX, MD, char* p, char* e)
{
	char* sep = strecbrk(p, e, ':');
	int uid, gid;

	if(sep == e)
		gid = -1;
	else
		gid = resolve_group(ctx, sep + 1, e);

	uid = resolve_user(ctx, p, sep);

	if(uid < 0 && gid < 0)
		return;

	sys_chown(md->path, uid, gid);
}

static void chmod_node(CTX, MD, char* p, char* e)
{
	int mode;

	if(p >= e)
		return;
	if(*p++ != '0')
		return;

	int len = (int)(e - p);
	char buf[len + 2];
	memcpy(buf, p, len);
	buf[len] = '\0';

	if(!(p = parseoct(buf, &mode)) || p < e)
		return;

	sys_chmod(md->path, mode);
}

static int isspace(int c)
{
	return (c == ' ' || c == '\t');
}

static char* endofword(char* p, char* e)
{
	while(p < e && !isspace(*p))
		p++;
	return p;
}

static char* skipspace(char* p, char* e)
{
	while(p < e && isspace(*p))
		p++;
	return p;
}

static void apply_mode(CTX, MD, char* ls, char* le)
{
	char* owns = ls;
	char* owne = endofword(owns, le);

	char* mods = skipspace(owne, le);
	char* mode = endofword(mods, le);

	ls = skipspace(mode, le);

	chmod_node(ctx, md, mods, mode);
	chown_node(ctx, md, owns, owne);
}

static char* matchpref(char* ls, char* le, int len, char* pref)
{
	if(le - ls < len + 1)
		return NULL;
	if(strncmp(ls, pref, len))
		return NULL;
	if(!isspace(ls[len]))
		return NULL;

	char* s = ls + len + 1;

	while(s < le && isspace(*s)) s++;

	return s;
}

static char* match(CTX, MD, char* ls, char* le)
{
	if(le - ls < 2)
		return NULL;
	if(*ls == '#' || isspace(*ls))
		return NULL;

	if(*ls == '~')
		return matchpref(ls+1, le, md->subsyslen, md->subsystem);
	else
		return matchpref(ls+0, le, md->devlen, md->devname);
}

static void locate_matching_line(CTX, MD)
{
	char *ls, *le, *lr;

	char* buf = ctx->config.buf;
	char* end = buf + ctx->config.len;

	for(ls = buf; ls < end; ls = le + 1) {
		le = strecbrk(ls, end, '\n');

		if(!(lr = match(ctx, md, ls, le)))
			continue;

		apply_mode(ctx, md, lr, le);
		break;
	}
}

void trychown(CTX, char* subsystem, char* devname)
{
	struct dev device, *md = &device;

	if(!devname)
		return;
	if(load_file(&ctx->config, UDEVCONF))
		return;

	md->subsystem = subsystem;
	md->subsyslen = subsystem ? strlen(subsystem) : 0;

	md->devname = devname;
	md->devlen = strlen(devname);

	md->basename = basename(devname);
	md->baselen = strlen(md->basename);

	FMTBUF(p, e, path, md->devlen + 10);
	p = fmtstr(p, e, "/dev/");
	p = fmtstr(p, e, md->devname);
	FMTEND(p, e);

	md->path = path;

	locate_matching_line(ctx, md);
}
