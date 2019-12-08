include ../rules.mk

$(eval $(call service,ptyhub))
$(eval $(call command,ptyctl))
$(eval $(call command,nestvt))

ptyhub: ptyhub.o ptyhub_ctrl.o ptyhub_proc.o
