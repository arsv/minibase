include ../rules.mk

$(eval $(call bin,mpac,1))
$(eval $(call bin,mpkg,1))

mpac: mpac.o mpac_common.o mpac_create.o mpac_extract.o mpac_list.o mpac_pack.o

mpkg: mpkg.o mpkg_common.o mpkg_config.o mpkg_deploy_idx.o mpkg_deploy_pkg.o \
	mpkg_deploy.o mpkg_remove.o \
