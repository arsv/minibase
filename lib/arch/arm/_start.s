.equ NR_exit, 1

.text
.align 4

.global _start
.global _exit

_start:
	mov     fp, $0
	ldr     a1, [sp]           /* argc */
	add     a2, sp, $4         /* argv */

	add     a3, a2, a1, lsl $2 /* &argv[argc] */
	add     a3, a3, $4         /* envp */

	bl      main
_exit:
	mov     r7, $NR_exit
	swi     $0
