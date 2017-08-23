#include <sys/file.h>
#include <sys/mman.h>
#include <sys/creds.h>

#include <string.h>
#include <format.h>

#include "msh.h"
#include "msh_cmd.h"

static int mapid(struct mbuf* mb, char* name)
{
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

		ne = parseint(un, &id);
		break;
	};

	if(!ne || *ne != ':')
		return -1;

	return id;
}

static int pwname2id(struct mbuf* mb, char* name)
{
	int id;
	char* p;

	if((p = parseint(name, &id)) && !*p)
		return id;

	return mapid(mb, name);
}

static int pwresolve(struct sh* ctx, char* pwfile,
                     int n, char** names, int* ids, char* notfound)
{
	struct mbuf mb;
	int err = 0, ret, i;

	if((ret = mmapfile(&mb, pwfile)) < 0)
		return error(ctx, "cannot mmap", pwfile, ret);

	for(i = 0; i < n; i++)
		if((ids[i] = pwname2id(&mb, names[i])) < 0)
			err |= error(ctx, notfound, names[i], 0);

	munmapfile(&mb);

	return err;
}

int cmd_setuid(struct sh* ctx)
{
	int uid;
	char* pwfile = "/etc/passwd";
	char* user;

	if(shift_str(ctx, &user))
		return -1;
	if(moreleft(ctx))
		return -1;
	if(pwresolve(ctx, pwfile, 1, &user, &uid, "unknown user"))
		return -1;

	return fchk(sys_setresuid(uid, uid, uid), ctx, user);
}

int cmd_setgid(struct sh* ctx)
{
	int gid;
	char* pwfile = "/etc/group";
	char* group;

	if(shift_str(ctx, &group))
		return -1;
	if(moreleft(ctx))
		return -1;
	if(pwresolve(ctx, pwfile, 1, &group, &gid, "unknown group"))
		return -1;

	return fchk(sys_setresgid(gid, gid, gid), ctx, group);
}

int cmd_groups(struct sh* ctx)
{
	char* pwfile = "/etc/group";
	int num = numleft(ctx);
	char** groups = argsleft(ctx);
	int gids[num];

	if(noneleft(ctx))
		return -1;
	if(pwresolve(ctx, pwfile, num, groups, gids, "unknown group"))
		return -1;

	return fchk(sys_setgroups(num, gids), ctx, NULL);
}
