/=../../

all = mpkg

include ../rules.mk
include $/config.mk

mpkg: mpkg.o mpkg_common.o mpkg_config.o mpkg_deploy_idx.o mpkg_deploy_pkg.o \
	mpkg_deploy.o mpkg_remove.o

-include *.d
