#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/sched.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/fpath.h>

#include <nlusctl.h>
#include <string.h>
#include <format.h>
#include <cmsg.h>
#include <util.h>

#include "common.h"
#include "mountd.h"

#define REPLIED 1

/* mount(2) semantics in Linux suck big time when it comes
   to user-initiated mounts. The call was clearly only made
   for mounting system volumes. In most cases, the user will
   not be able to write to the filesystem *even* with full
   write access to the underlying file.

   The code below does basically

       mkdir /mnt/$name
       mount /dev/$name /mnt/$name

   and subsequently

       umount /mnt/$name
       rmdir /mnt/$name

   with lots of checks in-between.

   In case of file (fd) mounts, /dev/loopN is set up before
   doing the mount, the same way GNU make does it with -o loop. */

int reply(int fd, int rep, int attr, char* value)
{
	char txbuf[200];

	struct ucbuf uc = {
		.brk = txbuf,
		.ptr = txbuf,
		.end = txbuf + sizeof(txbuf)
	};

	uc_put_hdr(&uc, rep);
	if(attr) uc_put_str(&uc, attr, value);
	uc_put_end(&uc);

	sys_send(fd, uc.brk, uc.ptr - uc.brk, 0);

	return REPLIED;
}

void* get_scm(struct ucbuf* uc, int type, int size)
{
	void* p = uc->brk;
	void* e = uc->ptr;
	struct cmsg* cm;

	if(!(cm = cmsg_get(p, e, SOL_SOCKET, type)))
		return NULL;
	if(cmsg_paylen(cm) != size)
		return NULL;

	return cmsg_payload(cm);
}

static void make_path(char* buf, int len, char* pref, char* name)
{
	char* p = buf;
	char* e = buf + len - 1;

	p = fmtstr(p, e, pref);
	p = fmtstr(p, e, name);

	*p = '\0';
}

static int make_mount_point(char* path)
{
	return sys_mkdir(path, 0700);
}

static int has_slashes(char* name)
{
	char* p;

	for(p = name; *p; p++)
		if(*p == '/')
			return 1;

	return 0;
}

static char* get_attr_name(struct ucmsg* msg)
{
	char* name;

	if(!(name = uc_get_str(msg, ATTR_NAME)))
		return NULL;
	if(has_slashes(name))
		return NULL;

	return name;
}

static int mount(int fd, char* name, int flags, struct ucred* uc, int isloop)
{
	int ret;
	int fst;

	int nlen = strlen(name);
	char devpath[10+nlen];
	char mntpath[10+nlen];

	char data[200];
	int datalen = sizeof(data);

	make_path(devpath, sizeof(devpath), "/dev/", name);
	make_path(mntpath, sizeof(mntpath), "/mnt/", name);

	if((fst = check_blkdev(name, devpath, isloop)) < 0)
		return fst;
	if((ret = prep_fs_options(data, datalen, fst, uc)))
		return ret;
	if((ret = make_mount_point(mntpath)) < 0)
		return ret;

	const char* fstype = fs_type_string(fst);

	if((ret = sys_mount(devpath, mntpath, fstype, flags, data)) >= 0)
		goto done;
	else if(ret != -EACCES)
		goto fail;

	flags |= MS_RDONLY;

	if((ret = sys_mount(devpath, mntpath, fstype, flags, data)) < 0)
		goto fail;
done:
	return reply(fd, 0, ATTR_PATH, mntpath);
fail:
	sys_rmdir(mntpath);
	return ret;
}

static int cmd_mount_dev(int fd, struct ucmsg* msg, struct ucbuf* ub)
{
	char* name;
	int flags = MS_NODEV | MS_NOSUID | MS_SILENT | MS_RELATIME;
	struct ucred* uc;

	if(!(name = get_attr_name(msg)))
		return -EINVAL;
	if(!(uc = get_scm(ub, SCM_CREDENTIALS, sizeof(*uc))))
		return -EINVAL;
	if(uc_get(msg, ATTR_RDONLY))
		flags |= MS_RDONLY;

	return mount(fd, name, flags, uc, 0);
}

static int cmd_mount_fd(int fd, struct ucmsg* msg, struct ucbuf* ub)
{
	int* ffd;
	int idx;
	int flags = MS_NODEV | MS_NOSUID | MS_SILENT | MS_RELATIME;
	char* base;
	struct ucred* uc;

	if(!(base = get_attr_name(msg)))
		return -EINVAL;
	if(!(uc = get_scm(ub, SCM_CREDENTIALS, sizeof(*uc))))
		return -EINVAL;
	if(!(ffd = get_scm(ub, SCM_RIGHTS, sizeof(int))))
		return -EINVAL;
	if((idx = setup_loopback(*ffd, base)) < 0)
		return idx;

	FMTBUF(p, e, name, 16);
	p = fmtstr(p, e, "loop");
	p = fmtint(p, e, idx);
	FMTEND(p, e);

	int ret = mount(fd, name, flags, uc, 1);

	if(ret < 0) unset_loopback(idx);

	return ret;
}

static int cmd_umount(int fd, struct ucmsg* msg, struct ucbuf* uc)
{
	int ret;
	char* name;
	int flags = 0;

	if(!(name = get_attr_name(msg)))
		return -EINVAL;

	int nlen = strlen(name);
	char mntpath[10+nlen];

	make_path(mntpath, sizeof(mntpath), "/mnt/", name);

	int idx = check_if_loop_mount(mntpath);

	if((ret = sys_umount(mntpath, flags)) < 0)
		if(ret != -EINVAL)
			return ret;
	if((ret = sys_rmdir(mntpath)))
		return ret;

	if(idx >= 0)
		unset_loopback(idx);

	return 0;
}

static const struct cmd {
	int cmd;
	int (*call)(int fd, struct ucmsg* msg, struct ucbuf* uc);
} cmds[] = {
	{ CMD_MOUNT_DEV, cmd_mount_dev },
	{ CMD_MOUNT_FD,  cmd_mount_fd  },
	{ CMD_UMOUNT,    cmd_umount    }
};

static void close_all_cmsg_fds(struct ucbuf* uc)
{
	struct cmsg* cm;
	void* p = uc->brk;
	void* e = uc->ptr;

	for(cm = cmsg_first(p, e); cm; cm = cmsg_next(cm, e)) {
		if(cm->level != SOL_SOCKET)
			continue;
		if(cm->type != SCM_RIGHTS)
			continue;

		int* fp = cmsg_payload(cm);
		int* fe = cmsg_paylend(cm);

		for(; fp < fe; fp++)
			sys_close(*fp);
	}
}

static void dispatch_cmd(int fd, struct ucmsg* msg, struct ucbuf* uc)
{
	const struct cmd* p = cmds;
	const struct cmd* e = cmds + ARRAY_SIZE(cmds);
	int ret = -ENOSYS;

	for(; p < e; p++)
		if(p->cmd == msg->cmd) {
			ret = p->call(fd, msg, uc);
			break;
		}

	if(ret <= 0)
		reply(fd, ret, 0, NULL);
	/* else it's REPLIED */

	close_all_cmsg_fds(uc);
}

static void set_timer(struct itimerval* old, int sec)
{
	struct itimerval itv = {
		.interval = { 0, 0 },
		.value = { sec, 0 }
	};

	sys_setitimer(0, &itv, old);
}

static void clr_timer(struct itimerval* old)
{
	sys_setitimer(0, old, NULL);
}

static void set_urbuf(struct urbuf* ur, void* buf, int len)
{
	ur->buf = buf;
	ur->mptr = buf;
	ur->rptr = buf;
	ur->end = buf + len;
}

static void set_ucbuf(struct ucbuf* uc, void* buf, int len)
{
	uc->brk = buf;
	uc->ptr = buf;
	uc->end = buf + len;
}

void handle(int fd)
{
	char rxbuf[200];
	char control[64];
	struct itimerval itv;
	struct urbuf ur;
	struct ucbuf uc;
	int ret;

	set_urbuf(&ur, rxbuf, sizeof(rxbuf));
	set_ucbuf(&uc, control, sizeof(control));

	set_timer(&itv, 1);

	while((ret = uc_recvmsg(fd, &ur, &uc, 0)) > 0)
		dispatch_cmd(fd, ur.msg, &uc);

	if(ret < 0 && ret != -EBADF && ret != -EAGAIN)
		sys_shutdown(fd, SHUT_RDWR);

	clr_timer(&itv);
}
