#ifndef __SYSCALL6_H__
#define __SYSCALL6_H__

inline static long syscall6(int nr, long a1, long a2, long a3, long a4,
		long a5, long a6)
{
	register long x8 asm("r7") = nr;
	register long x0 asm("r0") = a1;
	register long x1 asm("r1") = a2;
	register long x2 asm("r2") = a3;
	register long x3 asm("r3") = a4;
	register long x4 asm("r4") = a5;
	register long x5 asm("r5") = a6;

	asm volatile ("svc 0" : "=r"(x0)
	: "r"(x8), "r"(x0), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5));
	
	return x0;
}

#endif
