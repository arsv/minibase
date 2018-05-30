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

# Startup code in assembly.
# The C section below expands into a similar block with .c in place of .s
# for each of the source subdirectories.

srcs-arch := $(sort $(wildcard arch/$(ARCH)/*.s))
objs-arch := $(patsubst %.s,%.o,$(srcs-arch))

$(foreach file,$(srcs-arch),\
	$(eval $(file:.s=.o): $(file)))

$(objs-arch): build-arch

.SILENT: build-arch
.PHONY: build-arch
.SECONDARY: build-arch # doesn't seem to work

build-arch: $(srcs-arch)
	$(MAKE) -C arch/$(ARCH)

build: build-arch

objs += $(objs-arch)
clean += arch/$(ARCH)/*.o

# Other subdirs

subdirs = $(filter-out arch, $(patsubst %/,%,\
              $(sort $(dir $(wildcard */Makefile)))))

define subdir
srcs-$1 = $$(sort $(wildcard $1/*.c))
objs-$1 = $$(patsubst %.c,%.o,$$(srcs-$1))

$$(foreach file,$$(srcs-$1),\
	$$(eval $$(file:.c=.o): $$(file)))

$$(objs-$1): build-$1

build-$1: $$(srcs-$1)
	$$(MAKE) -C $1

.SILENT: build-$1
.PHONY: build-$1
.SECONDARY: build-$1

build: build-$1

objs += $$(objs-$1)
clean += $1/*.o $1/*.d
endef

$(foreach d,$(subdirs),$(eval $(call subdir,$d,c)))

# Common targets

all.a: $(objs)
	$(AR) cr $@ $^

clean += */stamp all.a
