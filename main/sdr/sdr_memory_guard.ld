/*
 * The SDR sample dump engine writes 0x3ffe8000..0x3fff8000 of SRAM1 by hardware (see sdr_radio.c), so
 * nothing linked may live there. SRAM1 is also the IRAM range 0x400a0000..0x400bffff, mapped in reverse
 * word order: the dump region is IRAM 0x400a8000..0x400b7fff.
 */
ASSERT(_iram_end <= 0x400a8000, "SDR: IRAM reaches the sample dump region (IRAM 0x400a8000..0x400b7fff)")
ASSERT(_data_end <= 0x3ffe8000, "SDR: .data reaches the sample dump region (0x3ffe8000..0x3fff7fff)")
ASSERT(_bss_end <= 0x3ffe8000, "SDR: .bss reaches the sample dump region (0x3ffe8000..0x3fff7fff)")
