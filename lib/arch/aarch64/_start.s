.equ NR_exit, 93

.text
.align 4

.global _start
.global _exit

_start:
	mov     x30, 0              /* LR */
	mov     x29, sp             /* FP */

	ldr     x0, [sp]            /* argc */
	add     x1, sp, 8           /* argv */

	add     x2, x1, x0, lsl 3   /* &argv[argc] */
	add     x2, x2, 8           /* envp */

	bl      main
_exit:
	mov     x8, NR_exit
	svc     0
