/=../../

all = elfinfo

include ../rules.mk
include $/config.mk

elfinfo: elfinfo.o elfinfo_strings.o \
	elfinfo_general.o elfinfo_sections.o elfinfo_symbols.o \
	elfinfo_program.o elfinfo_dynamic.o

-include *.d
