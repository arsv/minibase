/=../../

all = shell

include ../rules.mk
include $/config.mk

shell: shell.o \
	shell_util.o \
	shell_cmds.o \
	shell_list.o \
	shell_proc.o \
	shell_file.o \
	shell_stat.o \
	shell_mount.o

-include *.d
