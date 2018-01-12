# This is slightly incorrect, but works well enough. If we let all.a
# alone be the default target, make may scan the objects and decide
# all.a is up to date *before* entering subdirectories are re-building
# those very objects. So just make sure to enter subdirectories first.

default: build all.a

include rules.mk

# The atrocity below makes sure directories are entered to build files
# inside and each directory is only entered once.
#
# Listing */*.c from the top directory while also allowing to build them
# from within each directory makes the build ambiguous, as the rules
# aren't guaranteed to match, and requires special compiler commands since
# gcc -c dir/file.c creates file.o instead of dir/file.o.

define subdir
srcs-$1 = $$(sort $(wildcard $1*.$2))
objs-$1 = $$(patsubst %.$2,%.o,$$(srcs-$1))

$$(foreach file,$$(srcs-$1),\
	$$(eval $$(file:.$2=.o): $$(file)))

$$(objs-$1): build-$1

.SILENT: build-$1

build-$1: $$(srcs-$1)
	$$(MAKE) -C $1

build: build-$1

objs += $$(objs-$1)
clean += $1*.o $1*.d
endef

subdirs = $(filter-out arch/, $(sort $(dir $(wildcard */Makefile))))

$(eval $(call subdir,arch/$(ARCH)/,s))
$(foreach d,$(subdirs),$(eval $(call subdir,$d,c)))

all.a: $(objs)
	$(AR) cr $@ $^

clean += */stamp all.a
