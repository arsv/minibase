include ../rules.mk

$(eval $(call bin,ctool,1))

ctool: ctool.o \
	ctool_bin.o \
	ctool_bin_parse.o \
	ctool_bin_write.o \
	ctool_lib.o ctool_lib_flist.o ctool_lib_index.o ctool_lib_match.o \
	ctool_remove.o
