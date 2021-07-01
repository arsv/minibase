.SECONDARY:
.PHONY: all clean

all: $(all)

%: %.o $/lib.a
	$(LD) -o $@ $(filter %.o,$^)

clean:
	rm -f *.o *.d $(all)
