.equ NR_exit, 60

.text
.global _start
.global _exit

_start:
	movq    0(%rsp),%rdi            /* %rdi = argc */
	leaq    8(%rsp),%rsi            /* %rsi = argv */
	call    main
	movq    %rax, %rdi
_exit:
	movq    $NR_exit, %rax
	movq    %rcx, %r10
	syscall
	hlt

.type _start,function
.type _exit,function

.section .note.GNU-stack,"",%progbits
