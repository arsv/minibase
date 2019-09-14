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
