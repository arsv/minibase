/=../../

all = ctool

include $/config.mk
include ../rules.mk

ctool: ctool.o \
	ctool_bin.o \
	ctool_bin_parse.o \
	ctool_bin_write.o \
	ctool_lib.o ctool_lib_flist.o ctool_lib_index.o ctool_lib_match.o \
	ctool_remove.o

-include *.d
