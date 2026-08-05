/* Coverage build only: collect the -fprofile-info-section pointers into a
   contiguous .gcov_info section and provide start/end symbols so the firmware
   can enumerate the per-translation-unit gcov_info structures at runtime.
   INSERT AFTER augments the default ESP-IDF linker script instead of replacing it. */
SECTIONS
{
    .gcov_info :
    {
        . = ALIGN(4);
        PROVIDE(__gcov_info_start = ABSOLUTE(.));
        KEEP(*(.gcov_info))
        PROVIDE(__gcov_info_end = ABSOLUTE(.));
    }
} INSERT AFTER .flash.rodata;
