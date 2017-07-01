/* Simple generic no-free heap. Pops up here and there. */

#define PAGE 4096

struct heap {
	void* brk;
	void* ptr;
	void* end;
};

void hinit(struct heap* hp, long size);
void hextend(struct heap* hp, long size);
void* halloc(struct heap* hp, long size);
