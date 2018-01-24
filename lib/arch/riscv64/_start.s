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

	slli    a2, a0, 3    /* 8*argc */
	add     a2, a2, a1   /* 8*argc + argv */
	addi    a2, a2, 8    /* 8*argc + argv + 8 = envp */

	jal     main
_exit:
	li      a7, NR_exit
	ecall

.type _start,function
.type _exit,function
