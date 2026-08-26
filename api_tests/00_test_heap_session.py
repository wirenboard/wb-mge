"""Heap-leak guard for the long no-reboot working session.

The suite runs against a single QEMU boot (the `qemu_process`/`api` fixtures are
session-scoped). conftest.pytest_collection_modifyitems pushes every device-reboot
test to the very end, so the body of the suite executes as one continuous session
without a heap reset. This module brackets that session:

  * test_heap_baseline  — forced first (marker `heap_baseline`); records free heap
    once boot is OVER and the device is quiescent. "Once boot is over" is not the same
    as "once the HTTP server answers", which is all conftest's readiness probe proves —
    see _wait_boot_settled for what is still to be started at that moment and for the
    false leak (measured at 19.5-20.8 KB) that sampling too early produced in CI.
  * test_heap_no_leak   — forced last of the no-reboot body (marker `heap_final`,
    placed just before the deferred reboot tests); records free heap again and
    asserts it did not drop by more than HEAP_LEAK_TOLERANCE_BYTES versus baseline.

Free heap fluctuates with in-flight allocations, so each measurement takes the max
free heap over a few samples (the most quiescent reading) to avoid flagging a
transient buffer as a leak. heap_min_free is reported for diagnostics only.
"""

import time

import pytest


# Allowed shrink of free internal heap from the start to the end of the no-reboot
# session. Kept as an absolute "not significantly below baseline" check so it stays
# portable across hosts. It absorbs legitimate, non-leaking retention (lazy caches,
# allocator fragmentation, the bounded auth-session ring buffer); it is not a per-test
# budget.
#
# THE ~37 KB OF MARGIN THIS COMMENT USED TO CLAIM IS NOT THERE, and nothing may be sized
# on it again. It read "the measured baseline->final delta across many full runs is a
# consistent ~+21 KB (free heap is HIGHER at the end: boot/init scratch is reclaimed
# early), with well under 1 KB run-to-run variance". A full 229-item local run on this
# tree measures delta = +1772 B: free heap ends 1.8 KB BELOW baseline, not 21 KB above
# it. Whatever produced the old figure, it does not reproduce here, so the real distance
# to this tolerance is ~14.6 KB, not ~37 KB. Do not re-derive a tolerance from a remembered
# delta — measure it, and note which host it was measured on.
#
# The variance that DOES exist is in the baseline, not in the final: the same commit
# measured final=267783 locally and final=267775 on the CI node (8 B apart) while their
# baselines differed by 19 000 B. _wait_boot_settled is what closes that.
HEAP_LEAK_TOLERANCE_BYTES = 16 * 1024

_SAMPLES = 4
_SAMPLE_GAP_S = 0.25

# WHY THE BASELINE WAITS FOR THE DEVICE TO FINISH BOOTING, AND IS NOT SIMPLY TAKEN WHEN THE
# HTTP SERVER ANSWERS.
#
# conftest declares the device ready as soon as GET /favicon.webp succeeds
# (_wait_for_qemu_ready in conftest.py) — i.e. as soon as http_server_init() has returned
# (main/main.c:219). Boot is NOT over at that point. virtual_io_init(), indication_init() and
# config_button_init() come after it, and so does port_manager_init() (main.c:261), which
# main.c:239-268 reaches only after a loop that samples the link state ONCE PER SECOND.
# Between them they start two UART drivers (rx/tx ring
# buffers, event queue, per-port RX buffers), the two bridge TCP servers, the cache Modbus TCP
# server (on by default, main/config.h:52) and four 3-4 KB task stacks.
#
# A baseline sampled in that window is not a baseline. It reads a device that has not finished
# booting, and every byte still to be allocated reappears at the end of the session as a
# "leak" — and `max` over the samples makes it worse, not better, because the earliest and
# highest pre-init sample is exactly the one it keeps.
#
# MEASURED, by delaying port_manager_init() by 8 s so the race is lost deterministically:
# baseline 288755 B -> final 269259 B, delta 19496 B, over the 16 KB tolerance, on a session
# that ran NO TESTS AT ALL between the two samples. CI build #21 reported baseline 288555 ->
# final 267775, delta 20780, with the whole suite in between — the same baseline to within
# 200 B. A full local run on the same commit measured baseline 269555 -> final 267783, i.e. a
# final 8 B from the node's. The node's end state was normal; only its baseline was wrong.
#
# 19.5-20.8 KB IS THE COST, AND IT IS A MEASURED NET, NOT A SUM OVER THE LIST ABOVE. Earlier
# revisions of this comment quoted "~46 KB", obtained by enumerating those allocations. That is
# a different quantity from the one that matters — the NET difference in free heap between a
# baseline sampled too early and one sampled after boot — and the two need not agree: part of
# the list is already allocated when the readiness probe answers, and boot scratch that is freed
# again nets out. The two numbers above are the measured ones and the only ones anything here is
# written against; the gross estimate is dropped rather than reconciled, because nothing is
# sized on it. One of the frees in that "nets out" is now identified and measured — the 9196 B
# of main-task stack and TCB that come back when app_main returns, see the accounting further
# down — but it accounts for 9 KB of a ~26 KB difference, so the gross estimate stays
# unreconciled and unused rather than becoming half-explained.
#
# THE WAIT NEEDS A POSITIVE SIGNAL, NOT JUST A QUIET HEAP, and getting that wrong was measured
# too. A first attempt waited only for free heap to stop falling — which is trivially true
# BEFORE the allocations start, because main.c:239-268 sits in a 1 s polling loop doing nothing.
# Against the same forced 8 s delay it declared boot settled after 2 s and reproduced the false
# leak exactly as before (baseline 288499, delta 19804). A quiet heap says "nothing is
# allocating right now", not "boot is over".
#
# THE PRIMARY POSITIVE SIGNAL IS /info's rs485_N.port_mode. That field reports the ACTIVE
# port_manager mode (main/info_handlers.c:121-124 -> port_manager_get_mode), and pm_ctx[] is a
# zero-initialised static (main/bridge/port_manager.c:56) with PM_MODE_DISABLED == 0
# (port_manager.h:31) — so before port_manager_init() has applied anything it reads "disabled"
# whatever NVS says. GET /settings answers from NVS and is available immediately. A SECOND,
# independent signal is needed for the one configuration in which that one is blind; it is
# introduced below.
#
# WHAT AGREEMENT ON BOTH PORTS PROVES, stated no more strongly than the code supports: that
# port_manager_init()'s port loop has reached the `pm_ctx[index].mode = mode` store
# (port_manager.c:429) for the LAST port. That store is after the cache Modbus server has been
# started (port_manager.c:737-741) and after the UART and the bridge/listener for that port,
# which is the bulk of the allocation — but it is NOT the end of the port's bring-up:
# cache_sync_global() (port_manager.c:453), which allocates the cache pool (up to 32 KB), still
# runs after it. So the agreement is a lower bound on progress rather than a completion signal,
# and the heap-stability window on top of it is what covers the tail. In practice that is
# comfortable — the tail is one allocation and the window is _BOOT_SETTLE_STABLE_S of quiet —
# but "the port loop has finished" is the claim not being made. (The pool is not hypothetical
# on a reused flash image: conftest does not restore cache_en_N either, for its own reasons —
# the cache_en paragraph of the _RS485_RESTORE_KEYS comment in conftest.py — so an overlay
# left enabled by an earlier run comes back with the image and brings that allocation with
# it.)
#
# WHAT MODE AGREEMENT CANNOT SEE: both ports configured `disabled`. Then wanted == active ==
# ["disabled", "disabled"] from the FIRST sample — the comparison is identically true before
# port_manager_init() has run exactly as loudly as after. It does not fail; it silently proves
# nothing. So AGREEMENT ALONE MAY NOT CERTIFY A BASELINE: _SETTLED requires a positive init
# signal as well. On every other configuration that costs nothing, because agreement there
# implies it (only port_init_mode()'s success path can make /info report a non-disabled mode).
#
# AND SUCH A BASELINE IS WRONG BY 28676 B — MEASURED — not by the "~4.9 KB, inside the tolerance, a
# bounded error" an earlier revision of this comment claimed. That claim rested on
# port_init_mode(i, PM_MODE_DISABLED) doing nothing, and it does not return early:
# `case PM_MODE_DISABLED: break;` (port_manager.c:371-373) falls through to
# `pm_ctx[index].mode = mode` (:429) and then to cache_sync_global() (:453) — which keys off
# pm_ctx[i].cache_overlay, NOT off the port mode, so with the overlay flag set it still calls
# cache_multimaster_enable() -> heap_caps_malloc(CACHE_MAX_ENTRIES * sizeof(cache_entry_t)) =
# 4096 x 8 = 32768 B (cache_multimaster.c:25, :58, :289). Nothing allocated that pool earlier:
# port_manager_init_subsystems() (main.c:192) only loads the flags and creates the mutexes
# (port_manager.c:606-619, :640), which the firmware's own comment at port_manager.c:588 states
# outright.
#
# THE NUMBER, both ports `disabled` with cache_en_1 left true in a reused image and
# port_manager_init() delayed 25 s so the race is lost deterministically: baseline 288475 B ->
# final 259799 B, DELTA 28676 B (28.0 KiB), on a session that ran nothing but a sleep in between
# — 12292 B past the 16384 B tolerance, reported as "Possible heap leak". An earlier revision
# split it into "~5100 B when the cache Modbus server starts (port_manager.c:737-741) and
# ~23600 B when the port loop runs"; the accounting below supersedes that split, because the
# second figure is a NET over a window that also contains a large free, not an allocation.
# (These figures are bytes, and the KiB conversions are binary: 28676 B is 28.0 KiB
# against a 16384 B = 16 KiB tolerance. An earlier revision quoted the same numbers as decimal
# "12.3 KB"/"28.7 KB" beside a binary tolerance, which is where the arithmetic stopped adding up.)
#
# THE ALLOCATIONS DO NOT ADD UP TO THAT ON THEIR OWN — 32768 + ~5100 IS MORE THAN 28676 — AND
# THE MISSING PIECE IS A FREE, NOT A HIDDEN ALLOCATION. Measured on this tree, one firmware
# build, both ports `disabled` in every row, free internal heap once settled:
#   (1) pre-init: port_manager_init() not yet run, cache server not started, no pool  288227 B
#   (2) post-init, cache server running, no pool                                      292691 B
#   (3) post-init, cache server init FORCED to fail, no pool                          297535 B
#   (4) post-init, cache server running, pool allocated                               259843 B
#   (5) as (2), but app_main parked in a loop so the main task is never deleted       283495 B
# (3)-(2) puts the cache Modbus TCP server at 4844 B. (2)-(4), from toggling rs485_1.cache_en
# over POST /settings on an otherwise idle device, puts the pool at 32848 B — the 32768 B of
# CACHE_MAX_ENTRIES x 8 plus allocator overhead, and 32772 B of it comes back on disable, so
# nothing about that allocation is hidden from this test. (2)-(5) is the piece the sum left out:
# 9196 B. app_main RETURNS as soon as port_manager_init() has run — main.c's wait-for-network
# loop is its last statement — FreeRTOS then deletes the main task, and
# CONFIG_ESP_MAIN_TASK_STACK_SIZE = 8192 B of stack plus ~1 KB of TCB and per-task overhead go
# back to the heap. A pre-init baseline is still holding all of that; the final sample is not.
# THAT 8192 IS THE QEMU BUILD'S (sdkconfig.qemu.minimal:1003) and this test only ever runs
# against QEMU (@pytest.mark.qemu). The hardware build sets 3584 (sdkconfig.mge_v3:1028), where
# the same free would be ~4.6 KB — the pool and the server cost the same there, so LESS is
# handed back and the net error would be some 4.6 KB LARGER, not smaller. Do not carry the
# 9196 B to a device.
#   32848 (pool) + 4844 (server) - 9196 (main task) = 28496 B
# against the 28676 B above and 28440 B when the whole thing was re-run here in one boot: a
# 75 s /info trace (162 samples) shows a plateau at 288227, ONE step down when
# port_manager_init() runs, then a plateau at 259787 to the end — 28440 B between the two
# plateau levels, of which -28412 B falls in the single adjacent sample pair that straddles
# the step. ("Plateau" is not bit-exact: samples inside one wander by a few tens of bytes,
# which is where the 28 B between 288227 - 28412 = 259815 and the 259787 level comes from.
# The plateau-to-plateau 28440 B is the figure to use; the one-pair -28412 B is quoted only
# to show the whole drop lands in a single interval, which is what the window argument below
# needs.) The earlier "4.9 + 32" was therefore not wrong about the allocations, it was
# INCOMPLETE — a sum over allocations is an upper bound on this error, never the error itself,
# which is why the measured net is what anything here is written against.
#
# THE ~200 B THAT DOES NOT RECONCILE IS NAMED RATHER THAN EXPLAINED AWAY: the computed 28496 B
# sits 180 B BELOW the 28676 B measurement and 56 B ABOVE the 28440 B trace, and rows (1)/(4)'s
# 288227/259843 sit 248 B BELOW and 44 B ABOVE the 25 s-delay run's 288475/259799. The rows
# come from
# DIFFERENT BOOTS, and a residual of ±200 B is ordinary allocator and NVS boot-to-boot spread —
# ~0.6 % of a 28 KB error, far below anything sized on these numbers, so it does not affect the
# conclusion. It is not evidence of a further allocation or free, and nothing here should be
# re-derived to make it vanish.
#
# ONE EXPLANATION WAS TRIED AND IS WRONG — do not bring it back. It said the pool is served from
# PSRAM (CONFIG_SPIRAM_USE_MALLOC, 32768 B being over CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL) and
# so is invisible to a MALLOC_CAP_INTERNAL counter.
#
# THERE IS NO PSRAM IN EITHER BUILD, AND THAT ALONE ENDS IT: `# CONFIG_SPIRAM is not set` in BOTH
# sdkconfigs (sdkconfig.qemu.minimal:944, sdkconfig.mge_v3:974), with CONFIG_SPIRAM_SUPPORT unset
# too (:2007 and :2054 respectively). No SPIRAM region is ever registered with the heap, and
# neither CONFIG_SPIRAM_USE_MALLOC nor CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL exists at all — the
# story is not "unlikely on a full heap", it is describing a feature this firmware is not built
# with. So a "the heap must have been fragmented that time" revival is not available either:
# there is no second region for the allocation to land in, fragmented or not.
#
# EVEN IF PSRAM WERE ENABLED the story would still not hold. ALWAYSINTERNAL is applied only inside
# heap_caps_malloc_default()/realloc_default() (IDF 5.4.4, components/heap/heap_caps.c:107-134),
# i.e. on the malloc() path; an explicit heap_caps_malloc(size, MALLOC_CAP_8BIT)
# (cache_multimaster.c:289-290) never goes through it. It reaches
# heap_caps_aligned_alloc_base() (heap_caps_base.c:127-170), which walks the priority list, and
# DRAM carries MALLOC_CAP_8BIT at priority 0 while SPIRAM carries it only at priority 2
# (components/heap/port/esp32/memory_layout.c:49,56) — so while a 32 KiB internal block exists,
# which at ~288 KB free it always does, the pool comes from DRAM. Row (4) above is the direct
# confirmation: /info sees the whole 32848 B. That claim also contradicted the paragraph before
# it, which argues the heap-stability window covers the cache_sync_global() tail: a window
# cannot cover an allocation the counter never sees.
#
# THE POSITIVE SIGNAL THAT SURVIVES BOTH PORTS BEING DISABLED is /info's
# cache_modbus_active_port. It reports cache_modbus_server_get_port() (info_handlers.c:385-386),
# i.e. the file-static s_port (cache_modbus_server.c:26) that only cache_modbus_server_init()
# writes (:430) and deinit clears (:443). /info's own comment (info_handlers.c:372-378) lists
# "before port_manager_init() runs (httpd answers throughout main.c's wait-for-network loop)"
# among the states a 0 stands for. Like mode agreement it is a LOWER BOUND on progress — the port
# loop still follows it — and, as there, the heap-stability window on top covers the tail. THAT
# ARGUMENT HAS TO BE MADE ON THE DELTA, NOT ON THE TOTAL: the window restarts on a drop over
# _BOOT_SETTLE_NOISE_B BETWEEN ADJACENT SAMPLES (~0.35 s apart, see the loop below), so a tail of
# any total size made of sub-1024 B steps would slip under it. This one is not shaped like that:
# the allocations in it are individually large — the 32768 B cache pool in ONE heap_caps_malloc,
# 3-4 KB task stacks created one at a time, the UART rx/tx ring buffers — each several times
# _BOOT_SETTLE_NOISE_B on its own, so whichever sample interval one lands in restarts the window.
# (Do not read a total back off that: the net over the whole window is SMALLER than the pool
# alone, because the main task's 9196 B come back inside it — see the accounting above.
# THE FREE COULD IN PRINCIPLE MASK A STEP, since the window compares NET movement between
# adjacent samples and an interval holding both the free and an allocation can come out
# positive. It is harmless here because of WHERE that free happens: FreeRTOS deletes the main
# task once app_main returns, i.e. after port_manager_init() has run, which is after the last
# boot allocation — so an interval it flattens is one whose allocations are already on the
# baseline's side, and the most it could hide is one interval's worth: the 9196 B it hands back
# plus the _BOOT_SETTLE_NOISE_B = 1024 B that would have gone unnoticed anyway, ~10.2 KB, well
# under the 16384 B tolerance. Even the worst placement of that free cannot become a false leak.)
#
# ITS ONE PREMISE, because there IS a second starter and this was watched happening: a POST
# /settings runs settings_update.c's release/acquire pair, which starts the server whenever
# cache_modbus_wanted_port() differs from the running one (settings_update.c:41-55) — in the
# experiment above, at device t=14.4 s, fourteen seconds before port_manager_init(). So the claim
# is "non-zero proves port_manager_init() ran" only BEFORE the first POST /settings of the
# session, and that is exactly where this wait runs: it is the call phase of the FIRST item, and
# the only settings traffic conftest issues ahead of it is a GET (the GET /settings in
# conftest.py's _rs485_session_baseline; _restore_rs485_settings has no setup body by design,
# as its docstring in the same file says). Anything that later adds a POST /settings to
# session setup breaks this premise and must revisit this signal.
#
# ONE CONFIGURATION IS LEFT WITH NO SIGNAL AT ALL: both ports `disabled` AND
# cache_modbus_server_enabled false. Nothing observable over REST changes when
# port_manager_init() runs there, so no wait can certify a baseline. The run says exactly that at
# the baseline (_NO_SIGNAL_POSSIBLE — decided from the first GET /settings, not after 30 s of
# pointless polling) and names the remedy: regenerate the flash image, which restores the
# compiled-in defaults (port_mode `tcp_bridge` on both ports, main/setting_items.c:234-235, NOT
# main/config.h, which carries no port_mode default at all; cache_modbus_server_enabled true,
# config.h:52). An unusable baseline that names its cause and its remedy is a good outcome; a
# silent ~23650 B error is not. (That figure follows from the rows above rather than from the
# 28676 B measurement, which was taken with the cache Modbus server ENABLED: in THIS
# configuration the server's 4844 B cannot exist — port_manager.c:736-753 starts it only when
# cache_server_enabled — so what is left is the 32848 B pool less the 9196 B the main task hands
# back. It also needs a cache overlay left enabled in NVS, which is the same reused-image case
# that gets you here; with no overlay the pre-init baseline is ~9196 B LOW instead, and a low
# baseline cannot produce a false leak. Nothing observable separates those two from here, which
# is exactly why the verdict is "no usable baseline" and not "a small, bounded error".)
#
# BOTH CONFIGURATIONS ARE REACHABLE, in a workflow this repo documents — so this is a real
# limitation, not a theoretical one. `make qemu-test` regenerates build/qemu_flash.bin on every
# run (qemu-create-flash-image, qemu.mk:148-152, a prerequisite of qemu-test-locked), so it
# always boots a blank NVS and the compiled-in defaults apply. A direct
# `pytest --qemu --qemu-skip-build`, which is exactly what 37_'s docstring prescribes for running
# one file, reuses the existing image instead, and the NVS the previous run wrote comes back with
# it — including cache_en_N, which is what puts the overlay allocations above on the post-
# readiness side of the baseline in the first place. Nothing restores port_mode or cache_en
# centrally; conftest's per-module restore deliberately excludes both (the two paragraphs on
# _RS485_RESTORE_KEYS in conftest.py that begin "Deliberately excludes port_mode" and "Also
# deliberately excludes cache_en"), so whatever the last run left is what the next one
# boots into. A teardown of 29_test_gateway_dual_port.py that ran to completion is USUALLY not
# how it happens: that file does set both ports `disabled` (29_:194-195), but it then restores
# them twice over —
# api.update_settings(original) at :198 writes rs485_N.port_mode back (the mapping is
# settings_manager.c:90; the WRITE path is process_rs485_settings(), :726, whose base-mapping
# loop at :744-757 calls save_setting_from_json() at :754 — NOT add_setting_to_json() in
# add_rs485_settings_to_json()'s loop at :697-701, which only SERIALISES the GET response)
# and :202-206 sets both modes explicitly. "Usually" rather than "never", because the
# second restore reads `original.get("rs485_N", {}).get("port_mode", "disabled")` (29_:203-204):
# if the snapshot itself lacked port_mode — the add_setting_to_json() failure _port_modes
# documents below, which drops a STRING setting from an otherwise healthy 200 — a fully
# successful teardown writes `disabled` to BOTH ports. The likelier routes are still a teardown
# that did not COMPLETE (a run interrupted between the disable and the restore) and one whose
# restore was REFUSED and not checked: 29_:199-200 only prints a non-200, and the `finally`
# block of 37_'s test_cache_server_deinit_with_active_polling wraps each of its four restore
# calls — set_port_cache, set_port_mode(disabled), update_settings, set_port_mode(original) —
# in a bare `except Exception: pass`.
#
# It is a WAIT, not a budget: on a host where boot finished before the readiness probe (the
# usual case locally) it costs _BOOT_SETTLE_STABLE_S once per session and changes no
# measurement.
#
# ON EXPIRY IT RETURNS A DIAGNOSIS, NOT A BOOLEAN, because the deadline expiring means two
# OPPOSITE things and a single `settled=False` conflated them — the earlier version took the
# baseline anyway and printed "boot allocations STILL RUNNING", which is FALSE in one of the two
# cases and pointed debugging at the wrong subsystem two hours later, when the leak assert fired
# on a baseline that was perfectly good.
#
#   (i) A PORT NEVER CAME UP. port_init_mode() reaches its `pm_ctx[index].mode = mode` store
#       only on the success path (port_manager.c:429) and every failure path returns before it,
#       deliberately leaving that port PM_MODE_DISABLED (port_manager.c:800-804); a port skipped
#       by ports_frozen() (port_manager.c:794-798) is never applied at all. /info then never
#       agrees with /settings and the wait burns its full 30 s — on a device that finished
#       booting long ago. The baseline is FINE.
#  (ii) port_manager_init() HAS NOT RUN YET, because main.c:239-268 is still polling the link
#       state once a second. The baseline really would be pre-init, ~20 KB high, and the
#       session would end in the build-#21 false leak.
#
# A QUIET HEAP DOES NOT TELL THOSE APART, and it is worth being explicit about that, because it
# is the obvious discriminator and it is the same fallacy rejected two paragraphs up: in case
# (ii) the heap is quiet precisely BECAUSE nothing has started allocating yet. Waiting for "the
# heap stopped falling" would classify the pre-init device as boot-complete — the exact error
# this whole wait exists to remove.
#
# WHAT DOES TELL THEM APART is a positive fact about /info that does not depend on the modes
# agreeing — and there are two of them, independent, either sufficient:
#   * ANY port reports a mode other than "disabled". pm_ctx[] is zero-initialised and
#     PM_MODE_DISABLED == 0, and the only writer of a non-disabled value is port_init_mode()'s
#     success path — so one non-disabled port is proof that port_manager_init() ran its port
#     loop, whatever the other port is doing.
#   * cache_modbus_active_port is non-zero — the signal introduced above, and the one that still
#     works when both ports are configured `disabled`, where the first never can.
# Case (i) has at least one of them; case (ii) can have neither. The heap window still applies on
# top, for the allocation tail after the signal.
#
# Hence six verdicts, two usable and four not:
#   * _SETTLED            — modes agree, an init signal HAS been seen, AND heap steady. The
#                           normal path. Requiring the signal here too is the whole fix for the
#                           both-ports-`disabled` hole: an agreement that proves nothing can no
#                           longer certify a baseline by itself.
#   * _MODES_UNCONVERGED  — modes never agreed, but an init signal was seen and the heap held
#                           steady for the window. Boot is over as far as anything observable
#                           says, so the run proceeds — accepting the baseline rather than
#                           failing on a device that finished booting long ago.
#                           THREE situations produce it and only two are diagnosed: case (i)
#                           above; a port frozen by the factory test; and — indistinguishably —
#                           a port that was STILL coming up at the deadline and merely happened
#                           to allocate nothing in the last _BOOT_SETTLE_STABLE_S, whose
#                           remaining allocations would then land after the baseline. That third
#                           one is NOT separated out because nothing observable separates it: at
#                           the deadline all three read as "an init signal, no agreement, a quiet
#                           heap". Raise _BOOT_SETTLE_MAX_S if a node is slow enough to make it
#                           plausible.
#   * _NO_INIT_SIGNAL     — neither signal ever appeared. Case (ii) is one cause and NOT the only
#                           one: the port loop can also have run and left nothing observable
#                           behind, which happens when both ports are CONFIGURED `disabled` (no
#                           failure at all) and the cache Modbus server was enabled in NVS but
#                           could not bind — logged and non-fatal, port_manager.c:740-750. A
#                           port that genuinely failed to start is a third. What the verdict
#                           says is the same in all three, and it is the weak statement: there
#                           is no evidence boot finished, so the baseline is unusable. That is
#                           NOT the claim that the baseline is high — only case (ii) makes it
#                           high. The assert spells the cases out; do not collapse them again.
#   * _HEAP_UNSETTLED     — free heap was still falling at the deadline. Something is allocating
#                           right now; the baseline would be sampled mid-flight.
#   * _NO_SIGNAL_POSSIBLE — both ports configured `disabled` AND the cache Modbus server disabled
#                           in NVS: no init signal CAN exist on this device. Returned from the
#                           first GET /settings, without polling, since waiting cannot help.
#   * _WAIT_ERRORED       — nothing returned this; it is PUBLISHED BEFORE the wait starts and
#                           overwritten only once a baseline has actually been recorded, so an
#                           exception anywhere in test_heap_baseline still leaves a verdict
#                           behind. See there for why that matters.
# The four unusable verdicts fail (or error) in test_heap_baseline, at the moment they are
# detected and naming the cause, and test_heap_no_leak skips instead of accusing a leak it
# cannot evidence.
_BOOT_SETTLE_MAX_S = 30.0
_BOOT_SETTLE_STABLE_S = 3.0
_BOOT_SETTLE_POLL_S = 0.25
# A drop smaller than this is a request being served, not a subsystem starting up. Sized well
# under the smallest thing on the boot path (a 3072-byte task stack) and well over the churn of
# one /info round trip.
_BOOT_SETTLE_NOISE_B = 1024

_PORT_KEYS = ("rs485_1", "rs485_2")

# The verdicts. See the note above _BOOT_SETTLE_MAX_S for what each one means and, in particular,
# why the expiry cases are not one value. _WAIT_ERRORED is the only one _wait_boot_settled never
# returns — test_heap_baseline publishes it up front so a raising wait still leaves a diagnosis.
_SETTLED = "settled"
_MODES_UNCONVERGED = "modes-unconverged"
_NO_INIT_SIGNAL = "no-init-signal"
_HEAP_UNSETTLED = "heap-unsettled"
_NO_SIGNAL_POSSIBLE = "no-signal-possible"
_WAIT_ERRORED = "wait-errored"
# The verdicts on which a baseline may be sampled and compared against at the end of the
# session. Anything else fails in test_heap_baseline and skips test_heap_no_leak.
_USABLE_VERDICTS = (_SETTLED, _MODES_UNCONVERGED)

_MODE_DISABLED = "disabled"     # PORT_MODE_DISABLED_STR, main/setting_items.h:115
# /info's runtime port of the cache Modbus TCP server: 0 until port_manager_init() starts it.
_CACHE_PORT_KEY = "cache_modbus_active_port"
# /settings' NVS flag for the same server. Read once, to tell "the server is not up YET" from
# "the server is switched off, so it will never be a signal".
_CACHE_ENABLED_KEY = "cache_modbus_server_enabled"


def _port_modes(body, source):
    """The port_mode of both RS-485 ports out of a /settings or an /info payload.

    THE SHAPE IS ASSERTED, NOT DEFAULTED. A `.get(key, {}).get("port_mode", "disabled")` chain
    was the original form and it fails silently in the one way that matters: if either response
    ever stops carrying rs485_N.port_mode — a renamed field, a trimmed /info, an error body that
    still answers 200 — both sides would read ["disabled", "disabled"], the comparison would be
    identically true, and the whole positive signal would quietly degrade to the heap-only
    criterion that was measured and rejected. Cheap enough to check on every sample.
    """
    modes = []
    for key in _PORT_KEYS:
        port = body.get(key)
        assert isinstance(port, dict) and "port_mode" in port, (
            f"{source} carries no {key}.port_mode (got {port!r}) — the boot-settled guard "
            f"compares that field between /settings and /info, and a missing one would make "
            f"the comparison identically true instead of failing. On /settings this is "
            f"reachable without any rename: add_setting_to_json() (main/settings_manager.c:105) "
            f"omits a STRING setting outright when setting_items_read() fails, and port_mode is "
            f"one (setting_items.c:234-235), so an NVS read error drops the field from an "
            f"otherwise healthy 200"
        )
        modes.append(port["port_mode"])
    return modes


def _init_signal(info, active, require_cache_field):
    """True if this /info payload PROVES port_manager_init() has already entered its body.

    `active` is the port_mode pair already extracted from `info` by the caller.

    Two independent proofs, either sufficient — see the note over _BOOT_SETTLE_MAX_S:
      * a port reporting a non-disabled mode (port_init_mode()'s success path is the only
        writer of one);
      * the cache Modbus server bound to a port (port_manager_init() is the only thing that
        starts it before the first POST /settings).

    ONE-SIDED BY CONSTRUCTION, which is why the second is read with .get() rather than asserted
    the way _port_modes asserts its field: a missing cache_modbus_active_port can only make this
    return False, i.e. cost a 30 s wait and an explicit _NO_INIT_SIGNAL, never certify a baseline
    that should not have been. It IS asserted when it is the only proof available — both ports
    configured `disabled` — because there a silent False is a 30 s wait ending in a verdict that
    names the wrong cause.
    """
    if any(mode != _MODE_DISABLED for mode in active):
        return True
    active_port = info.get(_CACHE_PORT_KEY)
    assert not require_cache_field or isinstance(active_port, (int, float)), (
        f"GET /info carries no {_CACHE_PORT_KEY} (got {active_port!r}). Both RS-485 ports are "
        f"configured 'disabled', so that field is the ONLY signal left that can prove "
        f"port_manager_init() ran — mode agreement is identically true in this configuration "
        f"and proves nothing. Without it no heap baseline can be certified here"
    )
    return isinstance(active_port, (int, float)) and active_port > 0


def _wait_boot_settled(api):
    """Wait until port_manager has applied its modes AND free heap has stopped falling.

    Returns (verdict, samples, free) — see the note over _BOOT_SETTLE_MAX_S for the verdicts.
    `free` is the last sample and is what the failure reports are written against.

    Two things are tracked on EVERY sample rather than only while the modes agree, because
    both are needed to classify an expiry: heap stability, and whether an init signal has EVER
    been observed. Neither weakens the success condition — the window _SETTLED requires still
    starts no earlier than mode convergence AND the init signal (the max() below), so an idle
    pre-init stretch still cannot be counted as settled.
    """
    resp = api.get_settings()
    assert resp.status_code == 200, f"GET /settings returned {resp.status_code}"
    settings = resp.json()
    wanted = _port_modes(settings, "GET /settings")

    # Decided here rather than at the deadline: when every port is configured `disabled` the
    # cache Modbus server is the only possible init signal, and if IT is disabled too there is
    # nothing to wait FOR. Polling for 30 s would delay the same unusable verdict, not earn a
    # usable one.
    cache_server_only_signal = all(mode == _MODE_DISABLED for mode in wanted)
    if cache_server_only_signal:
        # THE FLAG IS ASSERTED, NOT DEFAULTED — the same standard _port_modes holds its field
        # to, and for the same reason. `.get()` cannot tell "the server is switched off" from
        # "the field is not in the payload", and the two deserve opposite reports: a missing
        # field would return _NO_SIGNAL_POSSIBLE from here with a message stating as fact that
        # the flag is false, and a remedy (regenerate the image) that would not address a
        # truncated response at all. It is asserted only on this branch, where the field is the
        # only thing standing between the run and an unusable baseline; everywhere else it is
        # not read, and a stricter check would only add a failure mode with nothing to gain.
        # Reachable without a rename: add_setting_to_json() (settings_manager.c:105-127) returns
        # false when the cJSON add fails, and every call site ignores that return
        # (settings_manager.c:165, :648, :700, :714), so the field simply vanishes from an
        # otherwise healthy 200. (A BOOL cannot vanish the way port_mode can — an NVS read error
        # yields the compiled-in default rather than dropping the key, settings_manager.c:116-118
        # — so under memory pressure is where to expect this.)
        cache_enabled = settings.get(_CACHE_ENABLED_KEY)
        assert isinstance(cache_enabled, bool), (
            f"GET /settings carries no usable {_CACHE_ENABLED_KEY} (got {cache_enabled!r}) — "
            f"expected a JSON boolean. Both "
            f"RS-485 ports are configured '{_MODE_DISABLED}', so that flag decides whether an "
            f"init signal can exist on this device at all: false means no wait can certify a "
            f"heap baseline here, true means the cache Modbus server's bound port is the one "
            f"signal left to wait for. Absent, it decides nothing, and guessing 'false' would "
            f"report a configuration fault the device may not have"
        )
        if not cache_enabled:
            return _NO_SIGNAL_POSSIBLE, 0, 0

    start = time.monotonic()
    deadline = start + _BOOT_SETTLE_MAX_S
    prev = None
    ready_since = None
    heap_steady_since = start
    init_observed = False
    samples = 0
    free = 0
    while True:
        resp = api.get_info()
        assert resp.status_code == 200, f"/info returned {resp.status_code}"
        info = resp.json()
        free = int(info["heap_free"])
        samples += 1
        now = time.monotonic()
        active = _port_modes(info, "GET /info")

        if prev is not None and free < prev - _BOOT_SETTLE_NOISE_B:
            heap_steady_since = now                 # something is still allocating
        # Latched: an init signal is proof about the PAST, and a port torn down later by a test
        # — or a cache server stopped by one — does not un-prove it.
        init_observed = init_observed or _init_signal(info, active, cache_server_only_signal)
        # BOTH conditions, deliberately. Mode agreement alone is identically true when both
        # ports are configured `disabled` and would certify a pre-init device; the init signal
        # alone says port_manager_init() started, not that it applied the modes.
        if active == wanted and init_observed:
            if ready_since is None:
                ready_since = now
            if now - max(ready_since, heap_steady_since) >= _BOOT_SETTLE_STABLE_S:
                return _SETTLED, samples, free
        prev = free

        if now >= deadline:
            if now - heap_steady_since < _BOOT_SETTLE_STABLE_S:
                return _HEAP_UNSETTLED, samples, free
            if not init_observed:
                return _NO_INIT_SIGNAL, samples, free
            return _MODES_UNCONVERGED, samples, free
        time.sleep(_BOOT_SETTLE_POLL_S)


def _free_heap_quiescent(api):
    """Return the max free internal heap over a few samples (most-quiescent reading),
    plus the last full /info payload for diagnostics."""
    best = -1
    data = None
    for _ in range(_SAMPLES):
        resp = api.get_info()
        assert resp.status_code == 200, f"/info returned {resp.status_code}"
        data = resp.json()
        free = int(data["heap_free"])
        if free > best:
            best = free
        time.sleep(_SAMPLE_GAP_S)
    return best, data


@pytest.mark.qemu
@pytest.mark.heap_baseline
# A WAIT, not a budget, and pytest.ini's 180 s is a budget. pytest-timeout charges setup +
# call + teardown to the item, and this one is the session's first, so all three are unusually
# large here:
#   setup    — the QEMU flash/efuse build, which qemu_process runs itself unless
#              --qemu-skip-build is passed (the "--- Build ---" block in conftest.py's
#              qemu_process) and which pulls in the full IDF build through
#              qemu-create-flash-image: build-idf-project-qemu (qemu.mk:148);
#              then QEMU startup (up to conftest's QEMU_READY_TIMEOUT = 900 s) and the
#              once-per-session GET /settings that _rs485_session_baseline takes (~15.9 s
#              measured on the CI node);
#   call     — another GET /settings and up to _BOOT_SETTLE_MAX_S of polling;
#   teardown — _restore_rs485_settings, whose module-scoped teardown fires inside the LAST item
#              of each module entry and so lands here, this file's first entry holding only
#              test_heap_baseline: 41.2 s ceiling (the "Resulting ceilings" block on
#              conftest.py's _RS485_HTTP_TIMEOUT).
@pytest.mark.timeout(1200)
def test_heap_baseline(api, request):
    """Record baseline free heap at the start of the continuous session.

    Waits for the post-HTTP boot allocations to land first — see _wait_boot_settled and the
    note above it. Without that the baseline can be ~20 KB too high and the session ends with
    a "leak" of exactly that size.

    A baseline that CANNOT be trusted fails HERE rather than being carried forward. The
    verdict is published either way, and test_heap_no_leak skips itself on the bad one: the
    fact is detected at this point and so must be reported at this point, while the leak
    assert has nothing left to compare against and must not manufacture a second, derivative
    failure out of a number it already knows is wrong.

    "EITHER WAY" INCLUDES RAISING, which is why the verdict is published UP FRONT and only
    overwritten once a baseline actually exists. Publishing it after the wait covered returned
    verdicts only, and every other exit from here is an exception: the status_code asserts, the
    shape assert in _port_modes, KeyError('heap_free'), or a ReadTimeout out of api_client
    (10 s per /info, and this wait issues up to ~120 of them during the most fragile phase of
    boot). With none published, test_heap_no_leak saw `verdict is None`, skipped its guard and
    failed with "pytest_collection_modifyitems ordering is broken" — sending the reader after a
    sorting hook that has nothing to do with it, which is the same wrong-direction recurrence
    the note over _BOOT_SETTLE_MAX_S documents. _WAIT_ERRORED is not in _USABLE_VERDICTS, so it
    degrades to that skip, and the traceback of the real exception stays the diagnosis. The
    same applies to the sampling below, not only to the wait — hence "once a baseline actually
    exists" rather than "once the wait returns".
    """
    request.config._heap_baseline_verdict = _WAIT_ERRORED
    verdict, settle_samples, settle_free = _wait_boot_settled(api)

    if verdict not in _USABLE_VERDICTS:
        # Name the real cause for test_heap_no_leak's skip. Only the UNUSABLE verdicts are
        # published here, deliberately: this test cannot get past the asserts below on one of
        # them, so there is no path on which a baseline is still to be recorded. Publishing a
        # usable verdict at this point would reopen the very hole above — _free_heap_quiescent
        # can raise too, and test_heap_no_leak would then read a usable verdict with no
        # baseline behind it and fall through to the "ordering is broken" assert.
        request.config._heap_baseline_verdict = verdict

    assert verdict != _NO_SIGNAL_POSSIBLE, (
        f"NO USABLE HEAP BASELINE IS POSSIBLE ON THIS DEVICE, and this is NOT a leak nor a "
        f"firmware fault. Both RS-485 ports are configured '{_MODE_DISABLED}' in NVS and "
        f"{_CACHE_ENABLED_KEY} is false, so NOTHING observable over REST changes when "
        f"port_manager_init() runs: /info's port_modes match /settings from the first sample "
        f"whether or not it has (both sides read '{_MODE_DISABLED}'), and {_CACHE_PORT_KEY} "
        f"stays 0 because that server is switched off. A baseline taken now is off by whatever "
        f"port_manager_init() has yet to do, and even the SIGN of that depends on NVS. With a "
        f"cache overlay (cache_en_N) left enabled it reads ~23650 B HIGH — the 32848 B pool "
        f"cache_sync_global() allocates even for a '{_MODE_DISABLED}' port, less the 9196 B "
        f"that come back when app_main returns and FreeRTOS deletes the 8192 B main task — "
        f"which the report prints as a drop of ~23650 B, i.e. ~7250 B past the "
        f"{HEAP_LEAK_TOLERANCE_BYTES} B tolerance, and would end the session in a false "
        f"'Possible heap leak'. With no overlay there is no "
        f"large allocation left and it reads ~9196 B LOW instead, which cannot produce a false "
        f"leak at all. Nothing observable here tells those two apart — hence 'no usable "
        f"baseline' rather than 'a small, bounded error'. The "
        f"cache Modbus server's own 4844 B is in neither figure: port_manager.c:736-753 starts "
        f"it only when the flag above is true. "
        f"WHAT TO DO: regenerate the flash image, which restores the compiled-in defaults "
        f"(tcp_bridge on both ports, cache server enabled) — `make qemu-test` does it on every "
        f"run, a direct `pytest --qemu --qemu-skip-build` does not. DROP THAT FLAG rather than "
        f"deleting build/qemu_flash.bin: qemu-create-flash-image is phony and regenerates the "
        f"image unconditionally (qemu.mk:148-152, :373), while with --qemu-skip-build still on "
        f"the command line conftest does not rebuild anything and exits on the missing file "
        f"instead (the flash/efuse existence check in conftest's qemu_process fixture), "
        f"killing the run with an unrelated error. Enabling one port, or the cache Modbus "
        f"server, over POST /settings and rebooting works too."
    )

    why = {
        _HEAP_UNSETTLED: (
            f"free internal heap was STILL FALLING (last sample {settle_free} B; a drop over "
            f"{_BOOT_SETTLE_NOISE_B} B landed inside the final {_BOOT_SETTLE_STABLE_S:.0f} s). "
            f"Something on this device is allocating right now, so a baseline sampled at this "
            f"moment WOULD read high by whatever that is, and the session would end with a "
            f"false 'Possible heap leak' of exactly that size — which is what CI build #21 was"
        ),
        _NO_INIT_SIGNAL: (
            f"NEITHER init signal ever appeared (last free-heap sample {settle_free} B): no "
            f"port reported a NON-DISABLED mode, and {_CACHE_PORT_KEY} stayed 0. pm_ctx[] is "
            f"zero-initialised and only port_init_mode()'s success path writes a non-disabled "
            f"value, while that port is only ever set by the cache_modbus_server_init() call "
            f"port_manager_init() makes above its port loop — so nothing observable is left "
            f"that could show the port loop ran. THAT IS AN ABSENCE OF PROOF, NOT A PROVEN "
            f"PRE-INIT DEVICE, and the reachable causes differ in exactly that respect. "
            f"(a) port_manager_init() HAS NOT RUN: main.c's once-per-second link-state loop "
            f"(main.c:239-268) is still waiting for the interface. Only here is the baseline "
            f"actually wrong — pre-init and ~20 KB high. Device log: 'Waiting for network "
            f"connection'. "
            f"(b) IT RAN AND LEFT NOTHING TO SEE: both ports are CONFIGURED '{_MODE_DISABLED}' "
            f"— which is not a failure, it is the stored mode, and no Port[N] init error will "
            f"be in the log because nothing was asked to come up — while the cache Modbus "
            f"server was enabled in NVS and could not bind. That is logged and deliberately "
            f"non-fatal (port_manager.c:740-750); a port collision with web_port or a bridge "
            f"port is the usual reason. Here port_manager_init() is OVER and the baseline is "
            f"sound; the run stops only because nothing can evidence it. Device log: "
            f"'cache_modbus_server_init(port ...) failed'. Cross-check GET /settings for both "
            f"rs485_N.port_mode and for cache_modbus_port. "
            f"(c) a port that WAS configured to come up failed to, and the cache server is off "
            f"or also failed — then, and only then, look for Port[N] init errors. "
            f"ONLY (a) MAKES THE BASELINE WRONG (pre-init, ~20 KB high, ending the session in "
            f"the false 'Possible heap leak' CI build #21 reported); on (b) and (c) it is sound "
            f"and merely unprovable. Read the device log before deciding which one this is"
        ),
    }.get(verdict)
    assert why is None, (
        f"NO USABLE HEAP BASELINE, and this is NOT a leak. The {_BOOT_SETTLE_MAX_S:.0f} s "
        f"boot-settled wait gave up after {settle_samples} samples without ever seeing boot "
        f"finish: {why}. The run stops here because the baseline cannot be TRUSTED — which, on "
        f"this verdict, is not necessarily the same as knowing it is wrong. test_heap_no_leak "
        f"skips itself rather than reporting a leak this run cannot evidence; fix the boot, or "
        f"raise _BOOT_SETTLE_MAX_S if this node is genuinely this slow."
    )

    free, data = _free_heap_quiescent(api)
    request.config._heap_baseline = free
    # Published only NOW, with the baseline it qualifies already recorded. Anything that raises
    # above this line leaves the up-front _WAIT_ERRORED standing, so test_heap_no_leak skips
    # with an accurate reason instead of blaming the collection order — see the docstring.
    request.config._heap_baseline_verdict = verdict
    if verdict == _SETTLED:
        note = f"boot allocations settled after {settle_samples} samples"
    else:
        note = (
            f"BASELINE ACCEPTED WITHOUT MODE CONVERGENCE: /info and /settings did not agree on "
            f"both port_modes for a full {_BOOT_SETTLE_STABLE_S:.0f} s inside the "
            f"{_BOOT_SETTLE_MAX_S:.0f} s wait ({settle_samples} samples), but an init signal DID "
            f"appear — a non-disabled port_mode, which only port_init_mode()'s success path can "
            f"produce, or a bound {_CACHE_PORT_KEY}, which only port_manager_init() can set — "
            f"and free heap then held steady for the last {_BOOT_SETTLE_STABLE_S:.0f} s. The "
            f"usual causes are benign and leave the baseline sound: a port that failed to "
            f"initialise, or one frozen by the factory test, keeps pm_ctx[].mode == "
            f"PM_MODE_DISABLED while /settings still reports what NVS says, and those two never "
            f"converge. A THIRD cause is not distinguishable from those at the deadline: a port "
            f"that was still coming up and merely allocated nothing in the last "
            f"{_BOOT_SETTLE_STABLE_S:.0f} s — its remaining allocations would land after this "
            f"baseline and read as a leak later. If the run does end in a leak of the size of "
            f"one port's bring-up, suspect that before suspecting the tests, and raise "
            f"_BOOT_SETTLE_MAX_S"
        )
    print(
        f"\n[heap] baseline free={free} B  "
        f"(total={data['heap_total']} B, min_free_since_boot={data['heap_min_free']} B, "
        f"{note})"
    )
    assert free > 0


@pytest.mark.qemu
@pytest.mark.heap_final
def test_heap_no_leak(api, request):
    """Assert free heap did not leak over the whole no-reboot session."""
    verdict = getattr(request.config, "_heap_baseline_verdict", None)
    if verdict is not None and verdict not in _USABLE_VERDICTS:
        # test_heap_baseline has already failed, loudly and with the diagnosis. Comparing
        # against a baseline it declared unusable could only produce a second red item
        # accusing a leak that the run has no evidence for — which is the exact failure mode
        # this whole wait exists to remove.
        pytest.skip(
            f"no usable heap baseline (verdict '{verdict}'), so test_heap_baseline failed and "
            f"there is nothing sound to compare against. That failure is the diagnosis; a leak "
            f"verdict computed here would not be one. ('{_WAIT_ERRORED}' means it did not even "
            f"get that far — it raised before recording a baseline, and its own traceback is "
            f"the diagnosis.)"
        )
    baseline = getattr(request.config, "_heap_baseline", None)
    assert baseline is not None, (
        "heap baseline was not recorded while the verdict says it should have been — "
        "pytest_collection_modifyitems ordering is broken, or test_heap_baseline never ran "
        "at all (its fixtures failed, so not even the up-front verdict was published)"
    )
    free, data = _free_heap_quiescent(api)
    delta = baseline - free  # positive => heap shrank over the session
    print(
        f"\n[heap] final free={free} B  baseline={baseline} B  "
        f"delta={delta} B (tolerance {HEAP_LEAK_TOLERANCE_BYTES} B)  "
        f"min_free_since_boot={data['heap_min_free']} B"
    )
    assert delta <= HEAP_LEAK_TOLERANCE_BYTES, (
        f"Possible heap leak: free internal heap dropped by {delta} B over the "
        f"no-reboot session (baseline {baseline} -> final {free}), exceeding the "
        f"{HEAP_LEAK_TOLERANCE_BYTES} B tolerance. Inspect tests that open "
        f"sockets/WebSockets/sniffer/cache servers for missing cleanup. "
        f"CONTEXT ON THE BASELINE, so it is not suspected for the wrong reason: "
        f"_wait_boot_settled reported '{verdict}' when it was taken, and only its two USABLE "
        f"verdicts can reach this assert. '{_SETTLED}' means port_manager had applied both port "
        f"modes, an init signal had been observed, AND free heap had been steady for "
        f"{_BOOT_SETTLE_STABLE_S:.0f} s. '{_MODES_UNCONVERGED}' means that agreement did not "
        f"HOLD for {_BOOT_SETTLE_STABLE_S:.0f} s before the deadline — it may never have landed "
        f"at all, typically one port that could not come up, or it may have landed too late to "
        f"complete the window — while the init signal was there anyway (so port_manager_init() "
        f"had run) and the heap had stopped moving, i.e. boot was over all the same. Both are "
        f"sound in the cases they are meant for; the ONE case '{_MODES_UNCONVERGED}' cannot rule "
        f"out is a port that was still coming up at the deadline, whose remaining allocations "
        f"landed AFTER the sample and so left the baseline HIGH by that much — which reads as a "
        f"leak of exactly one port's bring-up here, so check that first if it is the verdict "
        f"above. The baseline that WOULD explain a big delta — sampled while boot allocations "
        f"were still running, ~20 KB high, CI build #21 — is reported as '{_HEAP_UNSETTLED}' or "
        f"'{_NO_INIT_SIGNAL}' (the latter covers that case among others), and neither reaches "
        f"this line: they fail in test_heap_baseline and skip this test."
    )
