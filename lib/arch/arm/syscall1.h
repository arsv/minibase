#ifndef __SYSCALL1_H__
#define __SYSCALL1_H__

inline static long syscall1(int nr, long a1)
{
	register long r7 asm("r7") = nr;
	register long r0 asm("r0") = a1;

	asm volatile ("svc 0" : "=r"(r0) : "r"(r7), "r"(r0));
	
	return r0;
}

#endif
