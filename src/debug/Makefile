include ../rules.mk

$(eval $(call bin,errno,1))
$(eval $(call bin,hexdump,1))
$(eval $(call bin,strings,1))
$(eval $(call bin,elfinfo))

elfinfo: elfinfo.o elfinfo_strings.o \
	elfinfo_general.o elfinfo_sections.o elfinfo_symbols.o \
	elfinfo_program.o elfinfo_dynamic.o
