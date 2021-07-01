/ = ../../

all = nldump rtdump rtevt genlfams rtipv6ra

include ../rules.mk
include $/config.mk

nldump: nldump.o common.o
rtdump: rtdump.o common.o

genlfams: genlfams.o

-include *.d
