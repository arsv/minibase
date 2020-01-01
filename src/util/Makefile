include ../rules.mk

$(eval $(call bin,bcp,1))
$(eval $(call bin,cal,1))
$(eval $(call bin,cpy,1))
$(eval $(call bin,date,1))
$(eval $(call bin,del,1))
$(eval $(call bin,kill,1))
$(eval $(call bin,locfg,1))
$(eval $(call bin,pslist,1))
$(eval $(call bin,pstree,1))
$(eval $(call bin,sysinfo,8))
$(eval $(call bin,systime,8))

date: date.o date_find.o date_time.o

cpy: cpy.o cpy_tree.o cpy_file.o
