#!/usr/bin/env python3
"""Project wrapper around pyclang's idf_clang_tidy.

Pyclang 0.6.3's GCC_FLAGS_MAPPING does not cover everything xtensa-esp-elf-gcc
emits, so clang-tidy aborts every TU with `error: unknown argument`. We patch
the class attribute before invoking main().

Two adjustments:
  - strip -mdisable-hardware-atomics (gcc-only optimization knob)
  - strip -mlongcalls (pyclang renames it to -mlong-calls, which esp-clang's
    xtensa target rejects; the bare flag is just ignored by clang with a warning)
"""
import sys

from pyclang.runner import Runner

Runner.GCC_FLAGS_MAPPING['-mdisable-hardware-atomics'] = ''
Runner.GCC_FLAGS_MAPPING['-mlongcalls'] = ''

# Skip pyclang's `idf.py reconfigure` step: it regenerates compile_commands.json
# from the *currently active* sdkconfig, which on this project can disagree with
# the sdkconfig the build/ tree was actually compiled against (we have separate
# native and QEMU sdkconfigs). That mismatch flips preprocessor flags like
# QEMU_BUILD between 0/1 vs. what the headers in build/config/sdkconfig.h expect,
# producing spurious "unknown type" errors. We rely on the caller (make / CI)
# to ensure the CDB is current before invoking us.
Runner.idf_reconfigure = lambda self: self

from pyclang.scripts.idf_clang_tidy import main  # noqa: E402

if __name__ == '__main__':
    sys.exit(main())
