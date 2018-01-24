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
.set noreorder

/* "All userspace code in Linux is PIC"
    http://www.linux-mips.org/wiki/PIC_code

    The jump is used to get curren PC value into a general-purpose
    register (ra). The value is then stored to $gp and used to do
    PC-relative jump.

    GAS does actually check the prologue, and may issue warnings
    if it's done in non-standard way. */

__start:
_start:
	bltzal  $0,0f
	move    $fp, $zero      /* frame pointer */
0:
	.cpload $31
	move    $ra, $zero	/* return address */

	lw      $a0, 0($sp)	/* argc = *sp */
	addiu   $a1, $sp, 4	/* argv = sp + 4 */

	addiu   $a2, $a0, 1     /* ...  = argc+1 */
	sll     $a2, $a2, 2     /* ...  = 4*(argc+1) */
	add     $a2, $a2, $a1   /* envp = (void*)argv + 4*(argc+1) */

	addiu   $sp, $sp, -16   /* argument space */

	la      $t9, main
	jalr    $t9
	nop                     /* delay slot */

	move    $a0, $v0        /* main return is _exit's first arg */
_exit:
	li      $v0, NR_exit
	syscall

.size _exit,.-_exit
.size _start,_exit-_start
