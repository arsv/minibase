#include <nlusctl.h>

static char buf[1024];
static const char bytes[] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
static const char blong[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x20, 0x21, 0x22, 0x23, 0x24 };

int main(void)
{
	struct ucbuf ucb = {
		.brk = buf,
		.ptr = buf,
		.end = buf + sizeof(buf)
	}, *uc = &ucb;
	struct ucattr* at;

	uc_put_hdr(uc, 0x53550012);

	uc_put_flag(uc, 1);
	uc_put_str(uc, 2, "some string here");
	uc_put_int(uc, 3, 12345);
	uc_put_str(uc, 4, "some other string");
	uc_put_bin(uc, 5, (char*)bytes, sizeof(bytes));

	at = uc_put_nest(uc, 6);
	uc_put_int(uc, 7, 1122);
	uc_put_str(uc, 8, "nest 1");
	uc_end_nest(uc, at);

	at = uc_put_nest(uc, 6);
	uc_put_int(uc, 7, 3344);
	uc_put_str(uc, 8, "nest 2");
	uc_end_nest(uc, at);

	uc_put_bin(uc, 9, (char*)blong, sizeof(blong));

	uc_put_end(uc);

	uc_dump((struct ucmsg*)buf);

	return 0;
}
