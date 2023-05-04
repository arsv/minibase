#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/creds.h>
#include <sys/proc.h>
#include <sys/mount.h>

#include <config.h>
#include <util.h>
#include <main.h>

ERRTAG("init");

static void check_pid_one(void)
{
	if(sys_getpid() != 1)
		fail("not running as pid 1", NULL, 0);
}

static int open_something(void)
{
	int fd;

	if((fd = sys_open("/dev/null", O_RDWR)) >= 0)
		return fd;
	if((fd = sys_open("/", O_PATH)) >= 0)
		return fd;

	return -ENOENT;
}

static int dup_to_std_012(int fd)
{
	if((fd < 1) && (sys_dup2(fd, STDOUT) < 0))
		return -1;
	if((fd < 2) && (sys_dup2(fd, STDERR) < 0))
		return -1;
	if(fd > 2 && (sys_close(fd) < 0))
		return -1;

	return 0;
}

static void setup_std_fds(void)
{
	int fd, ret;

	if(sys_fcntl(STDERR, F_GETFD) >= 0)
		return;

	if((fd = open_something()) < 0)
		_exit(0xFE);
	if((ret = dup_to_std_012(fd)) < 0)
		_exit(0xFD);
}

static int mkdir(char* name, int mode)
{
	int ret;

	if((ret = sys_mkdir(name, mode)) >= 0)
		return ret;
	if(ret == -EEXIST)
		return 0;

	warn("mkdir", name, ret);

	return ret;
}

static void mount(char* dir, char* fstype, int flags)
{
	int ret;

	if(mkdir(dir, 0755) < 0)
		return;

	if((ret = sys_mount("none", dir, fstype, flags, NULL)) >= 0)
		return;
	if(ret == -EBUSY)
		return;

	fail("mount", dir, ret);
}

static void mount_basic_filesystems(void)
{
	mount("/sys", "sysfs", MS_NOSUID | MS_NOEXEC | MS_NODEV);
	mount("/run", "tmpfs", MS_NOSUID | MS_NOEXEC | MS_NODEV);
	mount("/proc", "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV);

	mount("/dev", "devtmpfs", MS_NOSUID | MS_NOEXEC);
}

static noreturn void invoke_next_stage(int argc, char** argv)
{
	char* path = BASE_ETC "/boot/sysinit";
	char* base = basename(path);
	char** envp = argv + argc + 1;

	argv[0] = base;

	int ret = sys_execve(path, argv, envp);

	fail(NULL, path, ret);
}

int main(int argc, char** argv)
{
	check_pid_one();

	setup_std_fds();

	mount_basic_filesystems();

	invoke_next_stage(argc, argv);
}
