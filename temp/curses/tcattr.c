#include <bits/ioctl/tty.h>
#include <sys/ioctl.h>

#include <printf.h>
#include <util.h>

struct dict {
	char* name;
	int bits;
	int mask;
} iflags[] = {
	{ "IGNBRK",   0000001, 0000001 },
	{ "BRKINT",   0000002, 0000002 },
	{ "IGNPAR",   0000004, 0000004 },
	{ "PARMRK",   0000010, 0000010 },
	{ "INPCK",    0000020, 0000020 },
	{ "ISTRIP",   0000040, 0000040 },
	{ "INLCR",    0000100, 0000100 },
	{ "IGNCR",    0000200, 0000200 },
	{ "ICRNL",    0000400, 0000400 },
	{ "IUCLC",    0001000, 0001000 },
	{ "IXON",     0002000, 0002000 },
	{ "IXANY",    0004000, 0004000 },
	{ "IXOFF",    0010000, 0010000 },
	{ "IMAXBEL",  0020000, 0020000 },
	{ "IUTF8",    0040000, 0040000 },
}, oflags[] = {
	{ "OPOST",    0000001, 0000001 },
	{ "OLCUC",    0000002, 0000002 },
	{ "ONLCR",    0000004, 0000004 },
	{ "OCRNL",    0000010, 0000010 },
	{ "ONOCR",    0000020, 0000020 },
	{ "ONLRET",   0000040, 0000040 },
	{ "OFILL",    0000100, 0000100 },
	{ "OFDEL",    0000200, 0000200 },
}, cflags[] = {
	{ "B0",       0000000, 0010017 },
	{ "B50",      0000001, 0010017 },
	{ "B75",      0000002, 0010017 },
	{ "B110",     0000003, 0010017 },
	{ "B134",     0000004, 0010017 },
	{ "B150",     0000005, 0010017 },
	{ "B200",     0000006, 0010017 },
	{ "B300",     0000007, 0010017 },
	{ "B600",     0000010, 0010017 },
	{ "B1200",    0000011, 0010017 },
	{ "B1800",    0000012, 0010017 },
	{ "B2400",    0000013, 0010017 },
	{ "B4800",    0000014, 0010017 },
	{ "B9600",    0000015, 0010017 },
	{ "B19200",   0000016, 0010017 },
	{ "B38400",   0000017, 0010017 },
	{ "CS5",      0000000, 0000060 },
	{ "CS6",      0000020, 0000060 },
	{ "CS7",      0000040, 0000060 },
	{ "CS8",      0000060, 0000060 },
	{ "CSTOPB",   0000100, 0000100 },
	{ "CREAD",    0000200, 0000200 },
	{ "PARENB",   0000400, 0000400 },
	{ "PARODD",   0001000, 0001000 },
	{ "HUPCL",    0002000, 0002000 },
	{ "CLOCAL",   0004000, 0004000 },
	{ "BOTHER",   0010000, 0010000 },
}, lflags[] = {
	{ "ISIG",     0000001, 0000001 },
	{ "ICANON",   0000002, 0000002 },
	{ "XCASE",    0000004, 0000004 },
	{ "ECHO",     0000010, 0000010 },
	{ "ECHOE",    0000020, 0000020 },
	{ "ECHOK",    0000040, 0000040 },
	{ "ECHONL",   0000100, 0000100 },
	{ "NOFLSH",   0000200, 0000200 },
	{ "TOSTOP",   0000400, 0000400 },
	{ "ECHOCTL",  0001000, 0001000 },
	{ "ECHOPRT",  0002000, 0002000 },
	{ "ECHOKE",   0004000, 0004000 },
	{ "FLUSHO",   0010000, 0010000 },
	{ "PENDIN",   0040000, 0040000 },
	{ "IEXTEN",   0100000, 0100000 },
	{ "EXTPROC",  0200000, 0200000 },
};

static void dumpflags(char* tag, int val, struct dict* bits, int nb)
{
	struct dict* p;
	int n = 0;

	printf("%s: ", tag);

	for(p = bits; p < bits + nb; p++) {
		if((val & p->mask) == p->bits) {
			if(n++)
				printf(" | ");
			printf("%s", p->name);
		}
	}

	printf("\n");
}

int main(void)
{
	struct winsize ws;
	struct termios ts;
	int ret, i;

	if((ret = sys_ioctl(0, TIOCGWINSZ, &ws)) < 0)
		fail("ioctl", "TIOCGWINSZ", ret);

	printf("row=%i col=%i\n", ws.row, ws.col);

	if((ret = sys_ioctl(0, TCGETS, &ts)) < 0)
		fail("ioctl", "TCGETS", ret);

	dumpflags("iflag", ts.iflag, iflags, ARRAY_SIZE(iflags));
	dumpflags("oflag", ts.oflag, oflags, ARRAY_SIZE(oflags));
	dumpflags("cflag", ts.cflag, cflags, ARRAY_SIZE(cflags));
	dumpflags("lflag", ts.lflag, lflags, ARRAY_SIZE(lflags));

	printf("ispeed=%i ospeed=%i\n", ts.ispeed, ts.ospeed);

	printf("cc = { ");
	for(i = 0; i < sizeof(ts.cc); i++) {
		int q = ts.cc[i] & 0xFF;

		if(i) printf(", ");

		if(q >= 0x20 && q < 0x7F)
			printf("'%c'", q);
		else
			printf("0x%02X", q);
	}
	printf(" }\n");

	return 0;
}
