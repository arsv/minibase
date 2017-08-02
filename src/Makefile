/ = ../

include $/config.mk

bin = hello

include $/common.mk

# Recursive stuff; single level atm, so here and not in ../common.mk

subdirs = boot cmdops compat events init msh net user wifi
makesub = $(patsubst %,sub-%,$(subdirs))
cleansub = $(patsubst %,clean-%,$(subdirs))

rec: all subdirs

subdirs: $(makesub)

$(makesub): sub-%:
	$(MAKE) -C $*

clean-rec: clean $(cleansub)

$(cleansub): clean-%:
	$(MAKE) -C $* clean
