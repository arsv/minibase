/* NL shift buffer and related routines for receiving incoming
   stream of messages from the kernel. Intended usage:

	struct nrbuf nr;

	nr_buf_set(&nr, buf, sizeof(buf));
recv:
	if((ret = nr_recv(fd, &nr)) < 0)
		fail or return;
next:
	if(!(msg = nr_next(&nr)))
		goto recv;
	
	handle(msg);

	goto next;

   nr_recv does a single sys_recv() and therefore can be used both
   with blocking and non-blocking fds. The only difference is handling
   EAGAIN return. */

struct nrbuf {
	void* buf;
	void* msg;
	void* ptr;
	void* end;
};

int nl_recv(int fd, void* buf, int len);

void nr_buf_set(struct nrbuf* nr, void* buf, unsigned len);
int nr_recv(int fd, struct nrbuf* nr);
struct nlmsg* nr_next(struct nrbuf* nr);

/* Late (post-bind) subscription for event groups. Does a single ioctl
   on the netlink socket. GENL event group ids are dynamic, are require
   running commands through the socket before subscribing. */

int nl_subscribe(int fd, int id);
