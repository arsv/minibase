#include <bits/types.h>
#include <bits/time.h>
#include <syscall.h>

/* Ref. linux/include/uapi/linux/timex.h */

/* timex.mode */
#define ADJ_OFFSET      0x0001
#define ADJ_FREQUENCY   0x0002
#define ADJ_MAXERROR    0x0004
#define ADJ_ESTERROR    0x0008
#define ADJ_STATUS      0x0010
#define ADJ_TIMECONST   0x0020
#define ADJ_TAI         0x0080
#define ADJ_SETOFFSET   0x0100
#define ADJ_MICRO       0x1000
#define ADJ_NANO        0x2000
#define ADJ_TICK        0x4000

/* timex.status */
#define STA_PLL         0x0001
#define STA_PPSFREQ     0x0002
#define STA_PPSTIME     0x0004
#define STA_FLL         0x0008
#define STA_INS         0x0010
#define STA_DEL         0x0020
#define STA_UNSYNC      0x0040
#define STA_FREQHOLD    0x0080
#define STA_PPSSIGNAL   0x0100
#define STA_PPSJITTER   0x0200
#define STA_PPSWANDER   0x0400
#define STA_PPSERROR    0x0800
#define STA_CLOCKERR    0x1000
#define STA_NANO        0x2000
#define STA_MODE        0x4000
#define STA_CLK         0x8000

struct timex {
	uint modes;
	long offset;
	long freq;
	long maxerror;
	long esterror;
	int status;
	long constant;
	long precision;
	long tolerance;
	struct timeval time;
	long tick;
	long ppsfreq;
	long jitter;
	int shift;
	long stabil;
	long jitcnt;
	long calcnt;
	long errcnt;
	long stbcnt;
	int tai;
	int pad[11];
};

inline static long sys_adjtimex(struct timex* tx)
{
	return syscall1(NR_adjtimex, (long)tx);
}
