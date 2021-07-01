/ = ../

include $/config.mk

subdirs := \
	base \
	compat \
	crypto \
	curses \
	epoll \
	events \
	falloc \
	inotify \
	kmod \
	logs \
	lzma \
	mapper \
	netlink \
	nl80211 \
	nlusctl \
	printf \
	ptys \
	resolv \
	sntp \
	udev \
	user \
	util \
	wifi

build := $(patsubst %,build-%,$(subdirs))
clean := $(patsubst %,clean-%,$(subdirs))

all: $(build)

clean: $(clean)

$(build): build-%:
	$(MAKE) -C $*

$(clean): clean-%:
	$(MAKE) -C $* clean

.PHONY: all clean $(build) $(clean)
.SILENT: $(build) $(clean)
