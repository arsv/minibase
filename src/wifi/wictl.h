struct heap;
struct ucbuf;

struct top {
	int fd;
	struct heap hp;
	struct ucbuf tx;
	int socket;
	char cbuf[128];
};

void top_init(struct top* ctx);
struct ucmsg* send_recv(struct top* ctx);
struct ucmsg* send_check(struct top* ctx);

void cmd_status(struct top* ctx);
