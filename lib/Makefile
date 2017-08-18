default: all.a

include rules.mk

define depends
$1: $2
endef

define subdir
srcs-$1 = $$(sort $(wildcard $2/*.$3))
objs-$1 = $$(patsubst %.$3,%.o,$$(srcs-$1))

$$(foreach file,$$(srcs-$1),\
	$$(eval $$(call depends,$$(file:.$3=.o),$$(file))))

$$(objs-$1):
	$$(MAKE) -C $2

objs += $$(objs-$1)
clean += $1/*.o
endef

libdir = $(call subdir,$1,$1,c)
archdir = $(call subdir,arch,arch/$1,s)

$(eval $(call archdir,$(ARCH)))
$(eval $(call libdir,crypto))
$(eval $(call libdir,format))
$(eval $(call libdir,netlink))
$(eval $(call libdir,nlusctl))
$(eval $(call libdir,output))
$(eval $(call libdir,sigset))
$(eval $(call libdir,string))
$(eval $(call libdir,time))
$(eval $(call libdir,util))

all.a: $(objs)
	$(AR) cr $@ $^

clean += all.a
