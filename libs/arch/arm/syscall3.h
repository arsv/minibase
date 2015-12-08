#ifndef __SYSCALL3_H__
#define __SYSCALL3_H__

inline static long syscall3(int nr, long a1, long a2, long a3)
{
	register long r0 asm("r7") = nr;
	register long r1 asm("r0") = a1;
	register long r2 asm("r1") = a2;
	register long r3 asm("r2") = a3;

	asm volatile ("svc 0" : "=r"(r0)
		: "r"(r0), "r"(r1), "r"(r2), "r"(r3));
	
	return r0;
}

#endif
