/=../../

all = msh

include ../rules.mk
include $/config.mk

msh: msh.o msh_parser.o msh_common.o \
	msh_cmd_base.o \
	msh_cmd_envp.o \
	msh_cmd_exec.o \
	msh_cmd_file.o \
	msh_cmd_mount.o \
	msh_cmd_pcaps.o \
	msh_cmd_pcred.o \
	msh_cmd_prctl.o \
	msh_cmd_sched.o \
	msh_cmd_sleep.o \
	msh_cmd_wait.o

-include *.d
