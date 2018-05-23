.equ NR_exit, 5058

.text
.align 8
.globl __start
.globl _start
.globl _exit

# All userspace MIPS code in Linux is PIC. The prologue has to figure
# out the actual address of main() to jump to. Some heavy MIPS magic,
# taken straight from musl.
#
# Branch is used to get the PC+8 value into a general purpose register,
# namely $ra. That's the actual address of label 1, the first gpdword.
#
# The linker sets each .gpdword to the offset of the symbol relative
# to some unknown origin. The first gpdword contains its own offset.
# The actual load address of that gpdword is in $ra, so
#
#     origin = ($ra - 0($ra))
#     main = origin + 8($ra)
#
# The rest of the code fills argc, argv, envp and jumps to main.

.set noreorder

__start:
_start:
	bal     2f
	move    $fp, $0         /* delay slot */
1:
	.gpdword 1b
	.gpdword main
2:
	ld      $gp, 0($ra)
	dsubu   $gp, $ra, $gp   /* origin */
	ld      $t9, 8($ra)
	daddu   $t9, $t9, $gp   /* actual address of main */

	move    $ra, $zero

	ld      $a0, 0($sp)
	daddiu  $a1, $sp, 8     /* argv = sp + 8 */

	jalr    $t9
	nop                     /* delay slot */

	move    $a0, $v0        /* main return is _exit's first arg */
_exit:
	li      $v0, NR_exit
	syscall

.set reorder

.size _exit,.-_exit
.size _start,_exit-_start

.type _start,function
.type _exit,function
