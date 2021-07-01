/=../../

all = svchub svcctl
aux = z-badsrv z-trap z-tick

include ../rules.mk
include $/config.mk

svchub: svchub.o svchub_control.o svchub_reload.o svchub_output.o \
	svchub_monitor.o svchub_reboot.o

svcctl: svcctl.o

z-badsrv: z-badsrv.o
z-trap: z-trap.o
z-tick: z-tick.o

-include *.d
