include ../rules.mk

$(eval $(call command,errno,1))
$(eval $(call command,hexdump,1))
$(eval $(call command,strings,1))
$(eval $(call command,elfinfo))

elfinfo: elfinfo.o elfinfo_strings.o \
	elfinfo_general.o elfinfo_sections.o elfinfo_symbols.o \
	elfinfo_program.o elfinfo_dynamic.o
