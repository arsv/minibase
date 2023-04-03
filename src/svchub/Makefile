/=../../

all = svchub svcctl
aux = z-server z-badsrv z-noisy z-slowy

include ../rules.mk
include $/config.mk

svchub: svchub.o svchub_boot.o svchub_ctrl.o svchub_proc.o

svcctl: svcctl.o

z-server: z-server.o
z-badsrv: z-badsrv.o
z-noisy: z-noisy.o
z-slowy: z-slowy.o

-include *.d
