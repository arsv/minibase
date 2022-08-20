.equ NR_exit, 1

.text
.align 4

.global _start
.global _exit

_start:
	mov     fp, $0
	ldr     a1, [sp]           /* argc */
	add     a2, sp, $4         /* argv */
	bl      main
_exit:
	mov     r7, $NR_exit
	swi     $0

.type _start,function
.type _exit,function

.section .note.GNU-stack,"",%progbits
