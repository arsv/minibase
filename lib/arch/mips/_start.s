/* Register names. This results in rather weird disassembly
   (as in objdump -d), but it works. */
.equ zero, 0
.equ ra, 31
.equ sp, 29
.equ v0, 2
.equ a0, 4
.equ a1, 5
.equ a2, 6

.equ NR_exit, 4001

.text
.align 4
.globl __start
.globl _start
.globl _exit

__start:
_start:
	/* "All userspace code in Linux is PIC"
	    http://www.linux-mips.org/wiki/PIC_code */
	.set noreorder
	bltzal $0,0f
	nop
0:
	.cpload $31
	.set reorder
	move    $ra, $zero	/* stack frame */

	lw      $a0, 0($sp)	/* argc = *sp */
	addiu   $a1, $sp, 4	/* argv = sp + 4 */

	sll     $a2, $a0, 2     /* ...  = argc*4 */
	add     $a2, $a2, $a1   /* envp = (void*)argv + 4*argc */

	addiu   $sp, $sp, -16   /* argument space */

	la      $25, main
	jalr    $25
	move    $a0, $v0        /* main return is _exit's first arg */
_exit:
	li      $v0, NR_exit
	syscall

.size _exit,.-_exit
.size _start,_exit-_start
