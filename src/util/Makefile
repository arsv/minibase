include ../rules.mk

$(eval $(call command,bcp,1))
$(eval $(call command,cal,1))
$(eval $(call command,cpy,1))
$(eval $(call command,date,1))
$(eval $(call command,del,1))
$(eval $(call command,kill,1))
$(eval $(call command,locfg,1))
$(eval $(call command,pslist,1))
$(eval $(call command,pstree,1))
$(eval $(call command,sysinfo,8))
$(eval $(call command,systime,8))

date: date.o date_find.o date_time.o

cpy: cpy.o cpy_tree.o cpy_file.o
