/* Register names. This results in rather weird disassembly
   (as in objdump -d), but it works. */

.equ r0, 0
.equ r1, 1
.equ r2, 2
.equ r3, 3
.equ r4, 4

.equ NR_exit, 1

.text
.align 4
.globl __start
.globl _start
.globl _exit

__start:
_start:
	bl 1f
	.long 0x0
1:
	mflr    r4
	lwz     r3,0(r4)
	add     r4,r3,r4
	mr      r3,r1
	rlwinm  r1,r1,0,0,27
	li      r0,0
	stwu    r1,-16(r1)
	mtlr    r0
	stw     r0,0(r1)
	/* at this point r3 contains &argv[-1] */
	addi    r4,r3,4
	lwz     r3,0(r3)
	bl      main
	/* return value in r3 is already in the right register for syscall arg 1 */
_exit:
	li      r0, NR_exit
	sc

.size _exit,.-_exit
.size _start,_exit-_start

.type _start,function
.type _exit,function
