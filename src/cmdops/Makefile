include ../rules.mk

$(eval $(call cmdops,bcp,1))
$(eval $(call cmdops,cat,1))
$(eval $(call cmdops,clear,1))
$(eval $(call cmdops,cpy,1))
$(eval $(call cmdops,date,1))
$(eval $(call cmdops,del,1))
$(eval $(call cmdops,df,1))
$(eval $(call cmdops,du,1))
$(eval $(call cmdops,ff,1))
$(eval $(call cmdops,fn,1))
$(eval $(call cmdops,ls,1))
$(eval $(call cmdops,ren,1))
$(eval $(call cmdops,stat,1))
$(eval $(call cmdops,sync,1))
$(eval $(call cmdops,symlink,1))
$(eval $(call cmdops,sysinfo,8))
$(eval $(call cmdops,tail,1))
$(eval $(call cmdops,time,1))
$(eval $(call cmdops,which,1))
$(eval $(call cmdops,whoami))

date: date.o date_find.o date_time.o

cpy: cpy.o cpy_tree.o cpy_file.o
