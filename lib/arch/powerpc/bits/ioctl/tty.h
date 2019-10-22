#include <bits/ioctl.h>

#define TCGETA          _IOR('t', 23, char[20])
#define TCSETA          _IOW('t', 24, char[20])
#define TCSETAW         _IOW('t', 25, char[20])
#define TCSETAF         _IOW('t', 28, char[20])
#define TCGETS          _IOR('t', 19, char[44])
#define TCSETS          _IOW('t', 20, char[44])
#define TCSETSW         _IOW('t', 21, char[44])
#define TCSETSF         _IOW('t', 22, char[44])

#define TIOCSCTTY       0x540E
#define TIOCSWINSZ      _IOW('t', 103, char[8])
#define TIOCGWINSZ      _IOR('t', 104, char[8])

struct winsize {
	unsigned short row;
	unsigned short col;
	unsigned short xpixel;
	unsigned short ypixel;
};

#define NCCS 19

struct termios {
	unsigned iflag;
	unsigned oflag;
	unsigned cflag;
	unsigned lflag;
	unsigned char cc[NCCS];
	unsigned char line;
	unsigned ispeed;
	unsigned ospeed;
};

/* c_cc characters */
#define VINTR     0
#define VQUIT     1
#define VERASE    2
#define VKILL     3
#define VEOF      4
#define VMIN      5
#define VEOL      6
#define VTIME     7
#define VEOL2     8
#define VSWTC     9
#define VWERASE  10
#define VREPRINT 11
#define VSUSP    12
#define VSTART   13
#define VSTOP    14
#define VLNEXT   15
#define VDISCARD 16

/* c_iflag bits */
#define IGNBRK  0000001
#define BRKINT  0000002
#define IGNPAR  0000004
#define PARMRK  0000010
#define INPCK   0000020
#define ISTRIP  0000040
#define INLCR   0000100
#define IGNCR   0000200
#define ICRNL   0000400
#define IXON    0001000
#define IXOFF   0002000
#define IXANY   0004000
#define IUCLC   0010000
#define IMAXBEL 0020000
#define IUTF8   0040000

/* c_oflag bits */
#define OPOST  0000001
#define ONLCR  0000002
#define OLCUC  0000004
#define OCRNL  0000010
#define ONOCR  0000020
#define ONLRET 0000040
#define OFILL  0000100
#define OFDEL  0000200
#define NLDLY  0001400
#define NL0    0000000
#define NL1    0000400
#define NL2    0001000
#define NL3    0001400
#define TABDLY 0006000
#define TAB0   0000000
#define TAB1   0002000
#define TAB2   0004000
#define TAB3   0006000
#define XTABS  00006000
#define CRDLY  0030000
#define CR0    0000000
#define CR1    0010000
#define CR2    0020000
#define CR3    0030000
#define FFDLY  0040000
#define FF0    0000000
#define FF1    0040000
#define BSDLY  0100000
#define BS0    0000000
#define BS1    0100000
#define VTDLY  0200000
#define VT0    0000000
#define VT1    0200000


/*
#define PAGEOUT ???
#define WRAP    ???
 */

/* c_cflag bit meaning */
#define CBAUD   00377
#define B0       0000000
#define B50      0000001
#define B75      0000002
#define B110     0000003
#define B134     0000004
#define B150     0000005
#define B200     0000006
#define B300     0000007
#define B600     0000010
#define B1200    0000011
#define B1800    0000012
#define B2400    0000013
#define B4800    0000014
#define B9600    0000015
#define B19200   0000016
#define B38400   0000017

#define EXTA    0000016
#define EXTB    0000017
#define CSIZE  00001400
#define CS5    00000000
#define CS6    00000400
#define CS7    00001000
#define CS8    00001400
#define CSTOPB 00002000
#define CREAD  00004000
#define PARENB 00010000
#define PARODD 00020000
#define HUPCL  00040000
#define CBAUDEX 0000020

#define B57600   00020
#define B115200  00021
#define B230400  00022
#define B460800  00023
#define B500000  00024
#define B576000  00025
#define B921600  00026
#define B1000000 00027
#define B1152000 00030
#define B1500000 00031
#define B2000000 00032
#define B2500000 00033
#define B3000000 00034
#define B3500000 00035
#define B4000000 00036
#define CIBAUD  077600000
#define CMSPAR  010000000000
#define CRTSCTS 020000000000


/*#define IBSHIFT 16 */

/* c_lflag bits */
#define ISIG    0x00000080
#define ICANON  0x00000100
#define XCASE   0x00004000
#define ECHOE   0x00000002
#define ECHOK   0x00000004
#define ECHO    0x00000008
#define ECHONL  0x00000010
#define NOFLSH  0x80000000
#define IEXTEN  0x00000400
#define ECHOCTL 0x00000040
#define ECHOPRT 0x00000020
#define ECHOKE  0x00000001
#define FLUSHO  0x00800000
#define PENDIN  0x20000000
#define TOSTOP  0x00400000
#define EXTPROC 0x10000000
