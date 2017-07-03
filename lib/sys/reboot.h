#include <syscall.h>

#define RB_AUTOBOOT     0x01234567
#define RB_HALT_SYSTEM  0xcdef0123
#define RB_POWER_OFF    0x4321fedc

inline static long sys_reboot(int cmd)
{
	return syscall4(__NR_reboot, 0xfee1dead, 0x28121969, cmd, 0);
}
