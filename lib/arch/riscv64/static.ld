ENTRY(_start)

SECTIONS
{
  . = 0x10000 + SIZEOF_HEADERS;
  .text  : { *:_start.o(.text) *(.text.*) *(.rodata.*) }
  . = DATA_SEGMENT_ALIGN (CONSTANT (MAXPAGESIZE), CONSTANT (COMMONPAGESIZE));
  PROVIDE(_gp = .);
  .data  : { *(.data.*) }
  .bss   : { *(.bss.*) }

  .line           0 : { *(.line) }
  .debug          0 : { *(.debug.*) }
  /DISCARD/ : { *(.note.GNU-stack) *(.gnu_debuglink) *(.gnu.lto_*) *(.comment) }
}
