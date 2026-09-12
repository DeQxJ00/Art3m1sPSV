/* vita-elf-create appends SCE metadata to the executable segment. Keep a
 * full page of virtual space before writable data, independent of code size. */
SECTIONS { . = ALIGN(0x1000) + 0x1000; }
INSERT BEFORE .init_array;
