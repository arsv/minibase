include ../rules.mk

$(eval $(call command,runwith))
$(eval $(call system,findblk,8))
$(eval $(call system,switchroot,8))

findblk: findblk.o findblk_config.o findblk_locate.o findblk_match.o
