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
void send_check_empty(struct top* ctx);

void dump_status(struct top* ctx, struct ucmsg* msg);
void dump_scanlist(struct top* ctx, struct ucmsg* msg);

void put_psk_arg(struct top* ctx, char* ssid, char* pass);
void put_psk_input(struct top* ctx, char* ssid);
