#include <sys/ppoll.h>
#include <sys/file.h>
#include <sys/mount.h>
#include <sys/fprop.h>
#include <sys/fpath.h>
#include <sys/proc.h>
#include <sys/creds.h>

#include <string.h>
#include <config.h>
#include <util.h>
#include <main.h>

#include "rdinit.h"

ERRTAG("rdinit");

/* The point of rdinit (initramfs pid 1) is to wait until the real rootfs
   gets located, initialized and mounted, then exec into the real init there.

   See linux:Documentation/filesystems/ramfs-rootfs-initramfs.txt for some
   background. The idea here is quite simple, but there are a few non-obvious
   caveats. */

/* Soft abort: run a script before exiting. This is pretty much only for
   debugging; the script should spawn a shell to let the user poke around
   while still in initrd environment. */

void abort(CTX, char* msg, char* arg)
{
	char* script = INIT_ETC "/shell";
	char* argv[] = { script, NULL };
	warn(msg, arg, 0);

	if(ctx->flags & FL_NEWROOT)
		goto out;
	if(sys_access(script, X_OK) < 0)
		goto out;

	sys_execve(script, argv, ctx->envp);
out:
	_exit(0xFF);
}

/* Normally the kernel opens the initial console on fds 0-2 before spawning
   rdinit. However, if no suitable console device has been configured, the
   kernel will leave fds 0-2 free. Running pretty much anything with fd 2
   free is a very bad idea, so we try to stub it.

   Detecting free fd 2 means we have no console, so there's no point in
   printing error messages — no-one will see them anyway. */

static void check_std_fds(CTX)
{
	if(sys_fcntl(STDERR, F_GETFD) >= 0)
		return; /* if 2 is ok, then 0 and 1 must be valid as well */

	ctx->flags |= FL_BAD_FDS;
}

static void setup_fds_one(CTX)
{
	int fd;

	if(!(ctx->flags | FL_BAD_FDS))
		return;

	if((fd = sys_open("/dev/null", O_RDWR)) >= 0)
		goto gotfd;
	if((fd = sys_open("/", O_PATH)) >= 0)
		goto gotfd;
gotfd:
	if((fd < 1) && (sys_dup2(fd, STDOUT) < 0))
		goto panic; /* cannot set stdout */
	if((fd < 2) && (sys_dup2(fd, STDERR) < 0))
		goto panic; /* cannot set stderr */
	if(fd <= 2 || (sys_close(fd) >= 0))
		return;
panic:
	_exit(0xFF);
}

/* We attempt to re-open /dev/console after running the setup script.

   If we started with a console, that node is now in the old /dev, over-mounted
   by devtmpfs. Re-opening in this case does not do much, it should be the same
   device, but it frees the hidden device node.

   If we started with no console, we do this anyway to allow startup schemes
   where the setup script brings up a console device. */

static void setup_fds_two(CTX)
{
	int fd;

	if((fd = sys_open("/dev/console", O_RDWR)) < 0)
		return;

	ctx->flags &= ~FL_BAD_FDS;

	if(sys_dup2(fd, STDIN) < 0)
		goto panic;
	if(sys_dup2(fd, STDOUT) < 0)
		goto panic;
	if(sys_dup2(fd, STDERR) < 0)
		goto panic;
	if((fd <= 2) || (sys_close(fd) >= 0))
		return;
panic:
	_exit(0xFF);
}

/* When running with initramfs, the kernel will NOT attempt to mount devtmpfs
   even if configured to do so. Instead, it will create /dev and /dev/console
   which we'll have to over-mount with devtmpfs right away. Nice.

   We also mount /sys early, pretty much any sensible use case for rdinit will
   need it anyway. */

static int mkdir(char* name)
{
	int ret;

	if((ret = sys_mkdir(name, 0755)) >= 0)
		; /* created successfully */
	else if(ret == -EEXIST)
		ret = 0;
	else
		warn(NULL, name, ret);

	return ret;
}

static void mount(char* dir, char* fstype, int flags)
{
	int ret;

	if(mkdir(dir) < 0)
		return;

	if((ret = sys_mount("none", dir, fstype, flags, NULL)) >= 0)
		return;

	warn(NULL, dir, ret);
}

static void mount_dev_sys(void)
{
	mount("/dev", "devtmpfs", MS_NOSUID | MS_NOEXEC);
	mount("/sys", "sysfs",    MS_NOSUID | MS_NOEXEC | MS_NODEV);

	mkdir("/mnt");
}

/* Once the new root has been mounted under /mnt,
   move both /dev and /sys there. */

static void move_mount(char* path)
{
	char* newpath = path + 1;
	int ret;

	if((ret = sys_mount(path, newpath, NULL, MS_MOVE, NULL)) >= 0)
		return;

	warn("move-mount", newpath, ret);

	if((ret = sys_umount(path, MNT_DETACH)) >= 0)
		return;

	warn("umount", path, ret);
}

static void switch_root(CTX)
{
	int ret;
	char* dir = "/mnt";

	if((ret = sys_chdir(dir)) < 0)
		fail("chdir", dir, ret);

	move_mount("/dev");
	move_mount("/sys");

	clear_initramfs(ctx);

	if((ret = sys_mount(".", "/", NULL, MS_MOVE, NULL)) < 0)
		fail("mount", ". to /", ret);
	if((ret = sys_chroot(".")) < 0)
		fail("chroot", ".", ret);
	if((ret = sys_chdir("/")) < 0)
		fail("chdir", "/", ret);
}

static noreturn void invoke_next_stage(CTX)
{
	char* path = BASE_ETC "/boot/sysinit";
	char* base = basename(path);
	char** envp = ctx->envp;
	char** argv = ctx->argv;

	argv[0] = base;

	int ret = sys_execve(path, argv, envp);

	fail(NULL, path, ret);
}

static void check_pid_one(void)
{
	if(sys_getpid() != 1)
		fail("not running as pid 1", NULL, 0);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	memzero(ctx, sizeof(*ctx));

	ctx->argv = argv;
	ctx->envp = argv + argc + 1;

	check_pid_one();

	check_std_fds(ctx);

	mount_dev_sys();

	setup_fds_one(ctx);

	locate_devices(ctx);

	setup_fds_two(ctx);

	switch_root(ctx);

	invoke_next_stage(ctx);
}
