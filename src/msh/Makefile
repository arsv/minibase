bin = msh

include ../rules.mk

msh: msh.o msh_parse.o msh_environ.o msh_error.o msh_support.o msh_command.o \
	msh_cmd_base.o msh_cmd_envp.o msh_cmd_file.o msh_cmd_sleep.o \
	msh_cmd_proc.o msh_cmd_uidgid.o msh_cmd_setcap.o msh_cmd_secbit.o

msh_cmd.h: msh_cmd_*.c
	./msh_cmd.sh > $@

msh_command.o: msh_cmd.h

msh_*.o: msh.h
