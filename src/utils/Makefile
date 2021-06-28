include ../rules.mk

$(eval $(call bin,bincopy))
$(eval $(call bin,calendar))
$(eval $(call bin,copy))
$(eval $(call bin,date))
$(eval $(call bin,delete))
$(eval $(call bin,find))
$(eval $(call bin,list))
$(eval $(call bin,locfg))
$(eval $(call bin,pskill))
$(eval $(call bin,pslist))
$(eval $(call bin,pstree))
$(eval $(call bin,sync))
$(eval $(call bin,mntstat))
$(eval $(call bin,sysinfo))
$(eval $(call bin,systime))

date: date.o date_find.o date_time.o

copy: copy.o copy_tree.o copy_file.o
