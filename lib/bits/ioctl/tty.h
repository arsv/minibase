#include <bits/ioctl.h>

#define TCGETA          0x5405
#define TCSETA          0x5406
#define TCSETAW         0x5407
#define TCSETAF         0x5408
#define TIOCSCTTY       0x540E
#define TIOCGWINSZ      0x5413

struct winsize {
	unsigned short row;
	unsigned short col;
	unsigned short xpixel;
	unsigned short ypixel;
};

#define NCC 8

struct termio {
	unsigned short iflag;
	unsigned short oflag;
	unsigned short cflag;
	unsigned short lflag;
	unsigned char line;
	unsigned char cc[NCC];
};
