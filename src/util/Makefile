include ../rules.mk

$(eval $(call bin,bincopy,1))
$(eval $(call bin,calendar,1))
$(eval $(call bin,copy,1))
$(eval $(call bin,date,1))
$(eval $(call bin,delete,1))
$(eval $(call bin,find,1))
$(eval $(call bin,list,1))
$(eval $(call bin,locfg,1))
$(eval $(call bin,pskill,1))
$(eval $(call bin,pslist,1))
$(eval $(call bin,pstree,1))
$(eval $(call bin,sync,1))
$(eval $(call bin,mntstat,1))
$(eval $(call bin,sysinfo,1))
$(eval $(call bin,systime,1))

date: date.o date_find.o date_time.o

copy: copy.o copy_tree.o copy_file.o
