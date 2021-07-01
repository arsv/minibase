/ = ../../

all = bincopy calendar copy date delete find list locfg
all += pskill pslist pstree sync mntstat sysinfo systime

include ../rules.mk
include $/config.mk

bincopy: bincopy.o
calendar: calendar.o
copy: copy.o copy_tree.o copy_file.o
date: date.o date_find.o date_time.o
delete: delete.o
find: find.o
list: list.o
locfg: locfg.o
pskill: pskill.o
pslist: pslist.o
sync: sync.o
mntstat: mntstat.o
sysinfo: sysinfo.o
systime: systime.o

-include *.d
