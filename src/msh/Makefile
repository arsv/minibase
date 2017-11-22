include ../rules.mk

$(eval $(call command,msh))

msh: msh.o msh_parse.o msh_environ.o msh_error.o msh_support.o msh_command.o \
	$(patsubst %.c,%.o,$(sort $(wildcard msh_cmd_*.c)))

msh_cmd.h: msh_cmd_*.c
	./msh_cmd.sh > $@

msh_command.o: msh_cmd.h

msh_*.o: msh.h
