/=../../

all = elfinfo

include ../rules.mk
include $/config.mk

elfinfo: elfinfo.o \
	elfinfo_common.o \
	elfinfo_header.o \
	elfinfo_segments.o \
	elfinfo_sections.o \
	elfinfo_dynamic.o \
	elfinfo_symbols.o \
	elfinfo_strings.o \
	elfinfo_versym.o

-include *.d
