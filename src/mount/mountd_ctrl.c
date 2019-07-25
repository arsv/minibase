#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/timer.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/fprop.h>
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

#define MSG struct ucmsg* msg __unused
#define UX struct ucaux* ux __unused

static int reply(int fd, int rep, int attr, char* value)
{
	char txbuf[200];
	int ret;

	struct ucbuf uc = {
		.brk = txbuf,
		.ptr = txbuf,
		.end = txbuf + sizeof(txbuf)
	};

	uc_put_hdr(&uc, rep);
	if(attr) uc_put_str(&uc, attr, value);
	uc_put_end(&uc);

	if((ret = uc_send_timed(fd, &uc)) < 0)
		return ret;

	return REPLIED;
}

static void* get_scm(struct ucaux* ux, int type, int size)
{
	void* p = ux->buf;
	void* e = p + ux->len;
	struct cmsg* cm;

	if(!(cm = cmsg_get(p, e, SOL_SOCKET, type)))
		return NULL;
	if(cmsg_paylen(cm) != size)
		return NULL;

	return cmsg_payload(cm);
}

static struct ucred* get_scm_creds(struct ucaux* ux)
{
	return get_scm(ux, SCM_CREDENTIALS, sizeof(struct ucred));
}

static int* get_scm_fd(struct ucaux* ux)
{
	return get_scm(ux, SCM_RIGHTS, sizeof(int));
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

static char* get_attr_name(MSG)
{
	char* name;

	if(!(name = uc_get_str(msg, ATTR_NAME)))
		return NULL;
	if(has_slashes(name))
		return NULL;

	return name;
}

static int get_flag_attr(MSG, int key)
{
	return !!uc_get(msg, key);
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

	if((fst = check_blkdev(devpath, isloop)) < 0)
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
	sys_chmod(devpath, 0000);
	return reply(fd, 0, ATTR_PATH, mntpath);
fail:
	sys_rmdir(mntpath);
	return ret;
}

static int cmd_mount(int fd, MSG, UX)
{
	char* name;
	int flags = MS_NODEV | MS_NOSUID | MS_SILENT | MS_RELATIME;
	struct ucred* uc;

	if(!(name = get_attr_name(msg)))
		return -EINVAL;
	if(!(uc = get_scm_creds(ux)))
		return -EINVAL;
	if(get_flag_attr(msg, ATTR_RDONLY))
		flags |= MS_RDONLY;

	return mount(fd, name, flags, uc, 0);
}

static int cmd_mount_fd(int fd, MSG, UX)
{
	int* ffd;
	int idx;
	int flags = MS_NODEV | MS_NOSUID | MS_SILENT | MS_RELATIME;
	char* base;
	struct ucred* uc;

	if(!(base = get_attr_name(msg)))
		return -EINVAL;
	if(!(uc = get_scm_creds(ux)))
		return -EINVAL;
	if(!(ffd = get_scm_fd(ux)))
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

static int cmd_umount(int fd, MSG, UX)
{
	int ret;
	char* name;
	int flags = 0;

	if(!(name = get_attr_name(msg)))
		return -EINVAL;

	int nlen = strlen(name);
	char mntpath[10+nlen];
	char devpath[10+nlen];

	make_path(devpath, sizeof(devpath), "/dev/", name);
	make_path(mntpath, sizeof(mntpath), "/mnt/", name);

	int idx = check_if_loop_mount(mntpath);

	if((ret = sys_umount(mntpath, flags)) < 0)
		if(ret != -EINVAL)
			return ret;
	if((ret = sys_rmdir(mntpath)))
		return ret;

	if(idx >= 0)
		unset_loopback(idx);

	sys_chmod(devpath, 0600);

	return 0;
}

static int cmd_grab(int fd, MSG, UX)
{
	char* name;
	struct ucred* uc;
	int ret;

	if(!(name = get_attr_name(msg)))
		return -EINVAL;
	if(!(uc = get_scm_creds(ux)))
		return -EINVAL;

	int nlen = strlen(name);
	char devpath[10+nlen];

	make_path(devpath, sizeof(devpath), "/dev/", name);

	if((ret = grab_blkdev(devpath, uc)) < 0)
		return ret;

	return reply(fd, 0, ATTR_PATH, devpath);
}

static int cmd_release(int fd, MSG, UX)
{
	char* name;
	struct ucred* uc;
	int ret;

	if(!(name = get_attr_name(msg)))
		return -EINVAL;
	if(!(uc = get_scm_creds(ux)))
		return -EINVAL;

	int nlen = strlen(name);
	char devpath[10+nlen];

	make_path(devpath, sizeof(devpath), "/dev/", name);

	if((ret = release_blkdev(devpath, uc)) < 0)
		return ret;

	return reply(fd, 0, 0, NULL);
}

static const struct cmd {
	int cmd;
	int (*call)(int fd, MSG, UX);
} cmds[] = {
	{ CMD_MOUNT,     cmd_mount     },
	{ CMD_MOUNT_FD,  cmd_mount_fd  },
	{ CMD_UMOUNT,    cmd_umount    },
	{ CMD_GRAB,      cmd_grab      },
	{ CMD_RELEASE,   cmd_release   }
};

static void close_all_cmsg_fds(struct ucaux* ux)
{
	struct cmsg* cm;
	void* p = ux->buf;
	void* e = p + ux->len;

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

static int dispatch(int fd, struct ucmsg* msg, struct ucaux* ux)
{
	const struct cmd* p;

	for(p = cmds; p < ARRAY_END(cmds); p++)
		if(p->cmd == msg->cmd)
			return p->call(fd, msg, ux);

	return -ENOSYS;
}

void handle(int fd)
{
	char rxbuf[200];
	char control[64];
	struct ucmsg* msg;
	int ret;
	struct ucaux ux = { control, sizeof(control) };

	if((ret = uc_recvmsg(fd, rxbuf, sizeof(rxbuf), &ux)) < 0)
		goto err;
	if(!(msg = uc_msg(rxbuf, ret)))
		goto err;

	if((ret = dispatch(fd, msg, &ux)) > 0)
		goto out; /* replied already */
	if((ret = reply(fd, ret, 0, NULL)) >= 0)
		goto out;
err:
	sys_shutdown(fd, SHUT_RDWR);
out:
	close_all_cmsg_fds(&ux);
}
