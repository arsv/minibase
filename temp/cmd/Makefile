/=../../

all = cmd

include ../rules.mk
include $/config.mk

cmd: cmd.o cmd_input.o cmd_history.o cmd_parser.o \
	cmd_pararg.o cmd_exec.o cmd_tabtab.o cmd_envp.o \
	cmd_utils.o unicode.o

-include *.d
