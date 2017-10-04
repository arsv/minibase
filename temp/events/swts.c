#include <bits/input.h>
#include <sys/file.h>
#include <sys/ioctl.h>

#include <errtag.h>
#include <format.h>
#include <string.h>
#include <util.h>

ERRTAG("keys");

static const char* keydesc[] = {
	[0x00] = "LID",
	[0x01] = "TABLET_MODE",
	[0x02] = "HEADPHONE_INSERT",
	[0x03] = "RFKILL_ALL",
	[0x04] = "MICROPHONE_INSERT",
	[0x05] = "DOCK",
	[0x06] = "LINEOUT_INSERT",
	[0x07] = "JACK_PHYSICAL_INSERT",
	[0x08] = "VIDEOOUT_INSERT",
	[0x09] = "CAMERA_LENS_COVER",
	[0x0a] = "KEYPAD_SLIDE",
	[0x0b] = "FRONT_PROXIMITY",
	[0x0c] = "ROTATE_LOCK",
	[0x0d] = "LINEIN_INSERT",
	[0x0e] = "MUTE_DEVICE",
	[0x0f] = "PEN_INSERTED"
};

static const int nkeydesc = ARRAY_SIZE(keydesc);

static void list_key(int key)
{
	const char* name;
	char buf[50];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	if(key < nkeydesc && (name = keydesc[key]))
		p = fmtstr(p, e, name);
	else
		p = fmtint(p, e, key);

	*p++ = '\n';
	sys_write(STDOUT, buf, p - buf);
}

static void list_part(int base, int byte)
{
	int i;

	for(i = 0; i < 8; i++)
		if(byte & (1<<i))
			list_key(base + i);
}

static void list_keys(int fd)
{
	byte bits[4];
	int bitsize = sizeof(bits);
	uint i;

	memset(bits, 0, bitsize);

	if(sys_ioctl(fd, EVIOCGBIT(EV_SW, bitsize), bits) < 0)
		return;

	for(i = 0; i < sizeof(bits); i++)
		list_part(8*i, bits[i]);
}

int main(int argc, char** argv)
{
	int fd;

	if(argc != 2)
		fail("bad call", NULL, 0);
	if((fd = sys_open(argv[1], O_RDONLY)) < 0)
		fail("open", argv[1], fd);

	list_keys(fd);

	return 0;
}
