.equ NR_exit, 93

.text
.align 4

.global _start
.global _exit

_start:
	li      x8, 0        /* frame pointer */
	la      x3, _gp      /* global pointer */
	ld      a0, 0(sp)    /* argc */
	addi    a1, sp, 8    /* argv */

	jal     main
_exit:
	li      a7, NR_exit
	ecall

.type _start,function
.type _exit,function
