dirs = crypto date string

all: $(patsubst %,run-%,$(dirs))

clean: $(patsubst %,clean-%,$(dirs))

run-%: %/Makefile
	$(MAKE) -C $* run

clean-%: %/Makefile
	$(MAKE) -C $* clean
