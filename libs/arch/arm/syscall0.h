#ifndef __SYSCALL0_H__
#define __SYSCALL0_H__

inline static long syscall0(int nr)
{
	register long r0 asm("r7") = nr;

	asm volatile ("svc 0" : "=r"(r0) : "r"(r0));
	
	return r0;
}

#endif
