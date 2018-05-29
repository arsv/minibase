include ../rules.mk

$(eval $(call command,bcp,1))
$(eval $(call command,cal,1))
$(eval $(call command,cat,1))
$(eval $(call command,clear,1))
$(eval $(call command,chmod,1))
$(eval $(call command,chown,1))
$(eval $(call command,cpy,1))
$(eval $(call command,date,1))
$(eval $(call command,del,1))
$(eval $(call command,df,1))
$(eval $(call command,du,1))
$(eval $(call command,ff,1))
$(eval $(call command,fn,1))
$(eval $(call command,kill,1))
$(eval $(call command,ls,1))
$(eval $(call command,pslist,1))
$(eval $(call command,pstree,1))
$(eval $(call command,ren,1))
$(eval $(call command,stat,1))
$(eval $(call command,sync,1))
$(eval $(call command,symlink,1))
$(eval $(call command,sysinfo,8))
$(eval $(call command,systime,8))
$(eval $(call command,tail,1))
$(eval $(call command,time,1))
$(eval $(call command,which,1))
$(eval $(call command,whoami))

date: date.o date_find.o date_time.o

cpy: cpy.o cpy_tree.o cpy_file.o
