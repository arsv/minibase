dirs = crypto date string

all: $(patsubst %,all-%,$(dirs))

run: $(patsubst %,run-%,$(dirs))

clean: $(patsubst %,clean-%,$(dirs))

all-%: %/Makefile
	$(MAKE) -C $* all

run-%: %/Makefile
	$(MAKE) -C $* run

clean-%: %/Makefile
	$(MAKE) -C $* clean
