/=../../

all = mpkg

include ../rules.mk
include $/config.mk

mpkg: mpkg.o \
	mpkg_common.o mpkg_config.o mpkg_deploy.o \
	mpkg_index.o mpkg_flist.o mpkg_policy.o \
	mpkg_remove.o

-include *.d
