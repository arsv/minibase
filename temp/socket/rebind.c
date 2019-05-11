#include <bits/socket/unix.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/fpath.h>

#include <main.h>
#include <util.h>

ERRTAG("rebind");

static const char path[] = "./socket";

static void unlink_path(void)
{
	sys_unlink(path);
}

static void bind_first(void)
{
	int fd, ret;
	struct sockaddr_un addr = { AF_UNIX, "./socket" };

	if((fd = sys_socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		fail("socket", NULL, fd);
	if((ret = sys_bind(fd, &addr, sizeof(addr))) < 0)
		fail("bind 1", NULL, ret);

	sys_close(fd);
}

static void bind_second(void)
{
	int fd, ret;
	struct sockaddr_un addr = { AF_UNIX, "./socket" };

	if((fd = sys_socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		fail("socket", NULL, fd);
	if((ret = sys_bind(fd, &addr, sizeof(addr))) < 0)
		fail("bind 2", NULL, ret);
}

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;

	unlink_path();
	bind_first();
	bind_second();

	return 0;
}
