include ../rules.mk

$(eval $(call bin,elfinfo))

elfinfo: elfinfo.o elfinfo_strings.o \
	elfinfo_general.o elfinfo_sections.o elfinfo_symbols.o \
	elfinfo_program.o elfinfo_dynamic.o
