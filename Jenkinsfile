// CI node load generator - an experiment tool, not part of the product build.
//
// Why this branch exists: the QEMU e2e suite used to collapse under CPU contention
// (data-path tests reading back zero bytes). It is green for 16 consecutive builds now,
// but the shared node was idle for every one of those builds, so the regime that actually
// produced the failures has never been re-tested. Green-on-an-idle-node is not evidence
// that the contention bug is fixed. This job puts deliberate, measurable load on a chosen
// node so the e2e suite can be re-run against a machine that is genuinely busy, and the
// oversubscription factor can be dialled to the ~1.5x previously measured as enough to
// reproduce the cascade.
//
// THE INVARIANT THIS FILE IS BUILT AROUND: a green build must never mean "load was
// silently absent". The conclusion drawn from such a build ("e2e is green under load")
// would be false, and nothing downstream could tell it from a real result.
//
// That invariant is enforced by ONE measured property, not by a list of failure shapes:
// after the loop, the job knows how many SECONDS it actually spent loading the node, and
// it fails unless that covers enough of the requested window (see the coverage check at
// the end of the Load stage). This is deliberate. Earlier revisions enumerated specific
// ways the loop could spin without loading anything - a fast-failure streak, an iteration
// cap, an overrun trim - and every review round found another shape that was not on the
// list (an infrastructure failure on iteration 1; exiting on the deadline with the
// failure streak still below its threshold). Enumeration cannot be completed; measuring
// the thing we actually care about can.
//
// So the division of labour is strict, and worth keeping strict:
//   * COVERAGE is the ONLY check that decides whether a run counts. Nothing else sets
//     `invalid`.
//   * The streak counter, the iteration cap, the overrun trim and the deadline decide only
//     WHEN TO STOP EARLY. They are optimisations: they end a doomed run in minutes instead
//     of hours, and they must not condemn one. A run that loaded the node for 83% of its
//     window and then hit three fast failures at the end has measured exactly what this
//     experiment set out to measure - the suite degrading under load - and throwing that
//     result away because of the shape of its tail would be wrong.
//   * The disk floor is not a validity check at all. It protects the MEASURED run from an
//     ENOSPC that would surface as a firmware failure, and it too only stops the loop.
//
// Why the load is the QEMU e2e suite and not a compile: the configuration that
// historically failed was two QEMU suites co-resident on one node. A C compile competes
// for CPU only, whereas a second emulator competes for the things QEMU is actually
// sensitive to - scheduling latency, timer fidelity, and the slirp network path.
//
// Based on fix/vvzvlad-e2e-test-stabilization rather than main on purpose:
// WB_MGE_PORT_SLOT and the per-tree flock barrier exist only on that branch
// (`git grep -l WB_MGE_PORT_SLOT` finds nothing on main). Without them two QEMU runs on
// one host fight over fixed host ports, and the experiment would measure a port collision
// instead of CPU contention.
//
// PORT SLOT - the single most important correctness property of this job. The measured
// e2e stage on the base branch sets `WB_MGE_PORT_SLOT = "${env.EXECUTOR_NUMBER ?: 0}"`,
// i.e. its slot is its executor index. If this job took the same slot,
// conftest._check_no_stale_qemu() would see our ports occupied and call
// pytest.exit(returncode=1) on whichever run started second - the load generator would
// abort the very run it exists to load, and it would look like a firmware regression.
// So our slot is offset by a band base of 100.
//
// It is resolved on the JENKINS side, for the reason the base branch states for its own:
// EXECUTOR_NUMBER is certainly defined there, and is NOT guaranteed to survive into the
// container's shell. Relying on qemu_ports.py's own EXECUTOR_NUMBER fallback would be
// unsafe precisely because that fallback is silent - _slot_from_env() ends in
// `return 0, "default"`, so a variable that failed to reach the container yields slot 0,
// which is also the measured build's slot when it sits on executor 0. Passing the value
// through the `environment` directive is what makes it reach the container, and the
// preflight below asserts that it actually arrived rather than assuming it.
//
// Deliberately absent: no archiveArtifacts, no junit, no S3 upload, no release steps, no
// notifications, no copyArtifactPermission. This job is launched many times and thrown
// away; anything it published would be noise in the artifact store and could be mistaken
// for a real build result - the load suite's own pass/fail is meaningless here, since it
// is a resource consumer, not a test.
//
// Also deliberately absent: disableConcurrentBuilds(). Running several instances at once
// is how the oversubscription factor is dialled up, so concurrency is the feature here.
// api_tests/qemu_ports.py's docstring assumes the product Jenkinsfile's
// disableConcurrentBuilds() serialises same-branch builds; dropping it does not break the
// "one run per tree, one slot per concurrent run" contract, because Jenkins gives each
// concurrent build its own workspace (workspace@2, @3, ...) - the per-tree half - and its
// own executor, which yields a distinct slot via the band above.
//
// WHAT THIS JOB LEAVES ON THE NODE, since it is not nothing:
//   * a full ESP-IDF build tree (build/, release/, test artifacts) per workspace, and one
//     workspace PER CONCURRENT BUILD (workspace@2, @3, ...) because concurrency is
//     deliberate here. Disk is the one shared resource that can BREAK the measured run
//     rather than merely slow it: ENOSPC inside its `make qemu-test` surfaces as a
//     firmware failure. Hence both a df probe in the metrics and a free-space floor
//     checked before each iteration, plus `deleteDir()` when the stage ends. Tradeoff,
//     accepted knowingly: a clean workspace means the next build's first iteration pays a
//     full firmware build - more load per iteration, fewer iterations, and a
//     first-iteration duration not comparable with later ones.
//   * a Docker image. The declarative `dockerfile` agent tags the image by a hash of the
//     DOCKERFILE, not of the build context, so this job and the measured build share a tag
//     whenever their Dockerfiles match - which they do. That is safe ONLY because the
//     build context is also identical: `patches/` and `api_tests/requirements.txt` are
//     byte-for-byte the same on both branches today (verified with git diff). If anyone
//     edits those paths HERE, both jobs keep resolving to the one tag while their contexts
//     differ, and the MEASURED run could be handed an image built from this branch - a
//     firmware difference that would look like a regression in the suite under test.
//     DO NOT modify Dockerfile, patches/ or api_tests/requirements.txt on this branch.
//   * its console log, bounded by buildDiscarder below.

// Shared by the `when` guard and the Load stage so the two cannot disagree about whether
// there is work to do. Returns -1 for a value that is not a non-negative integer, which
// the Plan stage rejects outright: '1.5' and '90m' silently becoming 0 would mean a green
// build with no load, which is the one outcome this file must never produce. An EMPTY
// value is a separate, legitimate case - the branch's first build, before the parameter
// exists - and resolves to 0. `when` reads anything <= 0 as "no work".
int resolvedMinutes() {
    String raw = (params.MINUTES ?: '').toString().trim()
    if (!raw) {
        return 0  // first build of the branch: the parameter is not defined yet
    }
    if (!raw.isInteger()) {
        return -1
    }
    int value = raw.toInteger()
    return value < 0 ? -1 : value
}

// Used by BOTH the agent label and the banner, so the log can never claim a different
// target than the one the label actually asked for. Whitespace is trimmed because ' ' is
// truthy in Groovy: a copy-pasted trailing space would otherwise survive the elvis
// fallback and become a label no node matches, whose failure mode is a silent queue wait
// rather than an error message.
String resolvedTargetNode() {
    return ((params.TARGET_NODE ?: '').toString().trim() ?: 'build-node-1-vm')
}

// MUST stay in sync with the timeout in options{} below. Kept as a plain number there
// because a method call in the options directive is a parse risk not worth taking; kept
// here so Plan can reject a MINUTES that could only ever end in an ABORTED build.
int pipelineTimeoutMinutes() {
    return 360
}

// The band base. Anything below this is a slot the measured e2e build could also pick,
// since its slot is its executor index.
int slotBandBase() {
    return 100
}

// Upper sanity bound, kept in ONE place. 734 is qemu_ports._MAX_SLOT only on a host with
// the default ephemeral floor of 32768: the real ceiling is derived from that host's
// ip_local_port_range, so a host configured `25000 65535` allows only 0..249. This check
// is therefore a cheap early rejection of obvious nonsense (-1, 99999), not the authority
// - qemu_ports._parse_slot re-checks against the real floor and raises with the exact
// range for that host.
boolean slotIsObviouslyOutOfRange(int slot) {
    return slot < 0 || slot > 734
}

// Resolved Jenkins-side (see the header). Band base + executor index keeps us clear of
// the measured build's slots as long as the node has at most 100 executors; beyond that
// the bands would start to overlap and the base would need raising.
int resolvedSlot() {
    String explicit = (params.PORT_SLOT ?: '').toString().trim()
    if (explicit) {
        return explicit.toInteger()
    }
    String raw = (env.EXECUTOR_NUMBER ?: '').toString().trim()
    return slotBandBase() + (raw.isInteger() ? raw.toInteger() : 0)
}

pipeline {
    // `agent none` is load-bearing, not tidiness. With a pipeline-level agent, Jenkins
    // allocates the executor, runs `checkout scm` and runs `docker build` BEFORE any
    // stage's steps - so a MINUTES=0 build would still pull an image and occupy the node,
    // possibly while the measured 2-hour run is in progress. The agent therefore lives in
    // the Load stage, gated by `when { beforeAgent true }`.
    agent none

    options {
        // Bounds two unbounded waits. First, a typo in TARGET_NODE produces a label no
        // node matches, and the build then waits in the queue FOREVER and silently - with
        // no disableConcurrentBuilds() those would pile up invisibly. Second, a hung
        // `make qemu-test` is bounded by nothing: pytest-timeout covers individual tests,
        // not the firmware build or QEMU bring-up, so load could otherwise continue long
        // past the end of the measured run and land on unrelated jobs.
        // 6 HOURS - keep in sync with pipelineTimeoutMinutes() above.
        timeout(time: 6, unit: 'HOURS')
        // This job publishes no artifacts, so its logs are most of what it leaves behind.
        buildDiscarder(logRotator(numToKeepStr: '10'))
    }

    parameters {
        string(
            name: 'TARGET_NODE',
            defaultValue: 'build-node-1-vm',
            description: 'Node label to load. Point this at the node the e2e suite is actually occupying - loading the other node generates no contention for it. Prefer a concrete node name over the shared "devenv" label, which could place this build on either node.'
        )
        string(
            name: 'MINUTES',
            defaultValue: '0',
            description: 'Length of the load window in minutes; must be a non-negative integer, and 0 means do nothing and finish green. NOT a precise duration in either direction: the deadline is only checked between iterations, so the last one may overrun it, and an iteration that plainly cannot fit is not started, so real load can end early by up to the length of the longest iteration. Set it generously - the "COVERAGE" line at the end of the log reports how much of the window was actually under load.'
        )
        string(
            name: 'PORT_SLOT',
            defaultValue: '',
            description: 'Escape hatch only: forces WB_MGE_PORT_SLOT. Leave EMPTY (the default) so the slot is 100 + EXECUTOR_NUMBER, which cannot collide with the measured build slot of EXECUTOR_NUMBER.'
        )
    }

    stages {
        // Runs without a node (inherits `agent none`): `echo` and `error` need no
        // workspace, so the no-op and reject paths cost nothing at all - no executor, no
        // checkout, no docker build.
        stage('Plan') {
            steps {
                script {
                    int minutes = resolvedMinutes()
                    String rawMinutes = (params.MINUTES ?: '').toString().trim()

                    if (minutes < 0) {
                        error("MINUTES='${rawMinutes}' is not a non-negative integer. A value like '1.5' or '90m' " +
                              "would otherwise resolve to 0 and finish green with no load generated, which is " +
                              "indistinguishable from a successful experiment.")
                    }

                    // A window at or beyond the pipeline timeout can only ever end in an
                    // ABORTED build, hours later. Reject it here instead.
                    if (minutes >= pipelineTimeoutMinutes()) {
                        error("MINUTES=${minutes} is at or beyond the ${pipelineTimeoutMinutes()}-minute pipeline " +
                              "timeout, so the build would be aborted before finishing. Use a shorter window, or run " +
                              "several builds back to back.")
                    }
                    if (minutes > pipelineTimeoutMinutes() - 60) {
                        echo "WARNING: MINUTES=${minutes} leaves under an hour before the " +
                             "${pipelineTimeoutMinutes()}-minute pipeline timeout. The image build plus a final " +
                             "overrunning iteration may not fit, and the build would be aborted rather than " +
                             "reporting its coverage."
                    }

                    // Validated here rather than in the Load stage so a bad value fails
                    // before an executor is taken. Passing e.g. -1 or 99999 through would
                    // raise at module import inside EVERY iteration.
                    String explicitSlot = (params.PORT_SLOT ?: '').toString().trim()
                    if (explicitSlot) {
                        if (!explicitSlot.isInteger()) {
                            error("PORT_SLOT='${explicitSlot}' is not an integer. Leave it empty to use ${slotBandBase()} + EXECUTOR_NUMBER.")
                        }
                        int slot = explicitSlot.toInteger()
                        if (slotIsObviouslyOutOfRange(slot)) {
                            error("PORT_SLOT=${slot} is not a plausible slot. Leave it empty to use ${slotBandBase()} + EXECUTOR_NUMBER.")
                        }
                        if (slot < slotBandBase()) {
                            echo "WARNING: PORT_SLOT=${slot} is below the band base ${slotBandBase()}, so it is a slot the " +
                                 "measured e2e build could also pick (its slot is its EXECUTOR_NUMBER). If both land on " +
                                 "the same node and the same slot, whichever starts second aborts on the stale-QEMU " +
                                 "port preflight - and it will look like a firmware failure."
                        }
                    }

                    if (minutes == 0) {
                        echo "MINUTES='${rawMinutes}' (empty means the parameter does not exist yet, on the branch's " +
                             "first build) -> nothing to do, exiting green. No node allocated, no image built. " +
                             "Use 'Build with Parameters' and set MINUTES > 0 to generate load."
                    } else {
                        echo "Will load '${resolvedTargetNode()}' for ${minutes} min."
                    }
                }
            }
        }

        stage('Load') {
            // `beforeAgent true` is the load-bearing part: without it the condition is
            // evaluated only after the node and the image have already been taken, which
            // is exactly the cost this guard exists to avoid.
            when {
                beforeAgent true
                expression { resolvedMinutes() > 0 }
            }
            agent {
                // No `reuseNode` here: it only means "reuse the top-level agent's node and
                // workspace", and with `agent none` above there is no such node, so it
                // would be an inert directive that reads as if it did something.
                dockerfile {
                    label "${resolvedTargetNode()}"
                    args '--entrypoint=""'
                }
            }
            // Evaluated after the agent is allocated, so EXECUTOR_NUMBER is defined. The
            // `environment` directive is also what guarantees the value reaches the
            // container's shell - the assumption the base branch explicitly refuses to
            // make about EXECUTOR_NUMBER itself.
            environment {
                WB_MGE_PORT_SLOT = "${resolvedSlot()}"
            }
            steps {
                script {
                    // An iteration shorter than this did not meaningfully load the node,
                    // whatever its exit code.
                    final int LOAD_FLOOR_SECONDS = 300
                    final int NO_LOAD_LIMIT = 3
                    // A bound on the flow graph, NOT a health signal: each iteration adds
                    // several nodes to the pipeline flow graph, persisted in program.dat on
                    // the shared controller. Reaching the cap does not condemn the run -
                    // "one loaded iteration, one instant failure, repeat" gets here with
                    // coverage above 100%, i.e. the node was busy for the whole window, and
                    // the load suite's own failures say nothing (it is a resource consumer,
                    // not a test). Like every other stop condition it only ends the loop;
                    // coverage alone decides whether the run counts.
                    final int MAX_ITERATIONS = 200
                    // Fraction of the requested window that must actually have been under
                    // load. Derived from the overrun trim's own worst case: with a single
                    // iteration longer than half the window the trim legitimately stops
                    // after one round (request 120 min, iteration 70 min -> 58%), so a
                    // healthy run can sit just above 50%, while anything below it means
                    // most of the window was idle. Coverage ABOVE 100% is normal and not an
                    // error - the last iteration is never killed mid-flight, so it overruns.
                    final int MIN_COVERAGE_PCT = 50
                    // Free-space floor, in KiB, checked before each iteration. An ESP-IDF
                    // build tree is a couple of GB and several may be co-resident; stopping
                    // at 5 GiB leaves the measured run room rather than racing it to ENOSPC.
                    final long DISK_FLOOR_KB = 5L * 1024L * 1024L

                    int minutes = resolvedMinutes()
                    String targetNode = resolvedTargetNode()
                    int slot = resolvedSlot()

                    // Belt and braces: the explicit param was checked in Plan, but the
                    // computed band base + EXECUTOR_NUMBER could only be checked here,
                    // once an executor exists.
                    if (slotIsObviouslyOutOfRange(slot)) {
                        error("Resolved port slot ${slot} is not plausible (EXECUTOR_NUMBER='${env.EXECUTOR_NUMBER}'). Set PORT_SLOT explicitly.")
                    }

                    // NODE_NAME, not TARGET_NODE: the parameter is a LABEL, so an operator
                    // who passes 'devenv' (as the product Jenkinsfile uses) gets either
                    // node while the banner would still read 'devenv'. Co-residency is the
                    // claim this experiment rests on, and differing EXECUTOR_NUMBERs prove
                    // nothing without a common node - while identical ones on different
                    // nodes are normal rather than a collision. These come from Jenkins,
                    // not the container's shell, for the same reason the slot does.
                    String identity = "[LOADGEN-METRICS] where: NODE_NAME=${env.NODE_NAME} " +
                                      "EXECUTOR_NUMBER=${env.EXECUTOR_NUMBER} WB_MGE_PORT_SLOT=${slot} " +
                                      "JOB=${env.JOB_NAME}#${env.BUILD_NUMBER} WORKSPACE=${env.WORKSPACE}"

                    echo """
========================================================================
CI NODE LOAD GENERATOR
  requested label : ${targetNode}
  actual node     : ${env.NODE_NAME}
  window          : ${minutes} min (approximate - see MINUTES description)
  load command    : make qemu-test (QEMU e2e suite, looped)
  port slot       : ${slot} ${(params.PORT_SLOT ?: '').toString().trim() ? '(forced via PORT_SLOT)' : "(${slotBandBase()} + EXECUTOR_NUMBER)"}
========================================================================
"""
                    echo identity

                    // Machine metrics must come from INSIDE the container, unlike the
                    // identity line above. Prefixed so the series can be lifted out of the
                    // log with `grep LOADGEN-METRICS`. Measured on build-node-1-vm by
                    // build #7: nproc=16, 32 GB RAM, ~105 GB free disk, and loadavg
                    // 0.81/0.99/1.18 with one measured e2e build running - i.e. a single
                    // e2e suite leaves the node almost entirely idle, which is why
                    // several concurrent instances of this job are needed to reach the
                    // ~1.5x oversubscription that reproduces the cascade. Each probe is
                    // guarded because a missing tool must never abort a multi-hour run:
                    // `free` ships in procps, which is not guaranteed in the devenv image.
                    String metricsCmd = '''
echo "[LOADGEN-METRICS] nproc=$(nproc 2>/dev/null || echo unknown)"
echo "[LOADGEN-METRICS] loadavg=$(cat /proc/loadavg 2>/dev/null || echo unknown)"
if command -v free >/dev/null 2>&1; then
    free -m | sed 's/^/[LOADGEN-METRICS] free: /'
else
    echo "[LOADGEN-METRICS] free: procps not installed, falling back to /proc/meminfo"
    head -3 /proc/meminfo | sed 's/^/[LOADGEN-METRICS] free: /'
fi
if DF_OUT="$(df -Pk . 2>&1)"; then
    echo "$DF_OUT" | sed 's/^/[LOADGEN-METRICS] disk: /'
else
    echo "[LOADGEN-METRICS] disk: unavailable ($DF_OUT)"
fi
'''

                    echo '[LOADGEN] baseline measurement before any load'
                    sh metricsCmd

                    // PREFLIGHT: assert the slot, do not merely observe it. `make
                    // qemu-ports` exits 0 even when WB_MGE_PORT_SLOT never reached the
                    // container - qemu_ports would then fall back to EXECUTOR_NUMBER (the
                    // measured build's own slot: a direct collision) or to the silent
                    // `return 0, "default"`. Both print a perfectly healthy-looking line.
                    // So we match the NUMBER and the SOURCE, which is the last automatic
                    // path to an undetected collision.
                    //
                    // Captured into a variable rather than piped into grep because `make`
                    // must not sit on the LEFT of a pipe - the pipeline's exit status would
                    // be grep's and a failing make would go unnoticed. The output is
                    // captured with 2>&1 and echoed on BOTH paths because qemu.mk's own
                    // failure guidance ("could not derive the QEMU ports... run this to see
                    // the real error") is printed to STDOUT, so on the failure path it ends
                    // up in the variable; without the explicit echo, the error() below
                    // would tell the reader to consult output that never reached the log.
                    // Format per api_tests/qemu_ports.py port_summary():
                    //   "slot {SLOT} (from {SLOT_SOURCE}): web=... uart2=..."
                    int slotRc = sh(
                        returnStatus: true,
                        script: """
set -e
if ! OUT="\$(make qemu-ports 2>&1)"; then
    echo "\$OUT" | sed 's/^/[LOADGEN-METRICS] ports: /'
    exit 1
fi
echo "\$OUT" | sed 's/^/[LOADGEN-METRICS] ports: /'
echo "\$OUT" | grep -q "^slot ${slot} (from WB_MGE_PORT_SLOT)"
"""
                    )
                    if (slotRc != 0) {
                        error("Port-slot preflight failed (exit ${slotRc}): expected 'slot ${slot} (from WB_MGE_PORT_SLOT)'. " +
                              "Either make qemu-ports failed (its output is above, prefixed 'ports:'), or " +
                              "WB_MGE_PORT_SLOT did not reach the container and qemu_ports fell back to EXECUTOR_NUMBER " +
                              "or the default slot 0 - both of which can be the measured build's slot. Refusing to " +
                              "generate load: a run that collides with the suite it is meant to load would abort that " +
                              "suite and look like a firmware regression.")
                    }

                    // PREFLIGHT: build the frontend once, before the load window opens.
                    //
                    // Why this exists even though `make qemu-test` builds its own firmware:
                    // the firmware EMBEDS the built frontend from main/frontend/dist
                    // (favicon.webp, index.{html,js,css}.gz, roboto-*.woff2), and cmake
                    // fails at GENERATE time when those files are missing. The measured
                    // branch creates them in its own stage('Build frontend'), which runs
                    // before its firmware and e2e stages in the same workspace; this job
                    // goes straight to qemu-test, so nothing had ever created dist/.
                    //
                    // DO NOT REMOVE THIS AS REDUNDANT. It was found by this job's own
                    // no-load guard on its first real run (build #7): every iteration died
                    // in ~25s with "Cannot find source file: .../frontend/dist/favicon.webp"
                    // and the run was correctly reported as zero coverage. Deleting this
                    // step restores that failure exactly.
                    //
                    // Once per BUILD is enough: nothing in the loop deletes dist/ - the loop
                    // never runs `make clean` - and the frontend cannot change between
                    // iterations. A new build re-creates it naturally, since the stage ends
                    // with deleteDir().
                    //
                    // Fatal on failure, for the same reason the slot probe is: without dist/
                    // no iteration can ever load the node, so continuing would burn the
                    // whole window producing nothing.
                    //
                    // No `source /opt/esp/idf/export.sh` here: Makefile's build-frontend is
                    // `cd main/frontend && npm install && npm run build`, pure npm with no
                    // IDF tooling - which is why the measured branch also invokes it as a
                    // bare `sh 'make build-frontend'`. Matching it keeps the two identical.
                    //
                    // Placed BEFORE startEpoch is taken, so the npm install counts toward
                    // neither the deadline nor loadedSeconds: it is setup, not load.
                    //
                    // Output is captured rather than piped (same reason as make above: on
                    // the left of a pipe its status is lost) and echoed in full on failure;
                    // on success only the tail, because npm install is thousands of lines
                    // and the interesting part is the build result. The postcondition is
                    // asserted directly - dist/ present and non-empty - rather than trusting
                    // the exit code, since that is the property the firmware build needs.
                    int frontendRc = sh(
                        returnStatus: true,
                        script: '''
set -e
if ! OUT="$(make build-frontend 2>&1)"; then
    echo "$OUT" | sed 's/^/[LOADGEN-METRICS] frontend: /'
    exit 1
fi
echo "$OUT" | tail -20 | sed 's/^/[LOADGEN-METRICS] frontend: /'
test -d main/frontend/dist
ls main/frontend/dist | sed 's/^/[LOADGEN-METRICS] frontend: dist\\/: /'
test -n "$(ls -A main/frontend/dist)"
'''
                    )
                    if (frontendRc != 0) {
                        error("Frontend preflight failed (exit ${frontendRc}): main/frontend/dist could not be built " +
                              "(its output is above, prefixed 'frontend:'). The firmware embeds those files, so cmake " +
                              "would fail at generate time on every single iteration - about 25s each - and the run " +
                              "would load the node for exactly zero seconds. Refusing to start the window.")
                    }

                    // Wall clock via the shell so the pipeline needs no script approval for
                    // JVM time APIs.
                    long startEpoch = sh(returnStdout: true, script: 'date +%s').trim().toLong()
                    long deadline = startEpoch + (minutes * 60L)
                    int iteration = 0
                    int noLoadStreak = 0
                    // THE measured quantity: seconds actually spent loading the node, and
                    // how many iterations contributed them. Accumulated only in the "this
                    // iteration really loaded" branch, so no failure shape can inflate them.
                    int loadedIterations = 0
                    long loadedSeconds = 0L
                    // High-water mark over iterations that ACTUALLY loaded the node. Using
                    // the last iteration's duration instead would defeat the purpose: after
                    // a 10s failure it predicts the next round takes 10s, so with 20s left
                    // it starts a full suite that runs for tens of minutes - the case where
                    // trimming matters most is exactly the case it would do nothing for.
                    long maxLoadedDuration = 0L
                    // Hoisted so the post-loop verdict can see how the loop ended and name
                    // it in the failure reason. None of them decides validity - they only
                    // record WHY the loop stopped.
                    boolean stepFailed = false
                    boolean diskLow = false
                    boolean streakStop = false
                    String stopReason = 'requested window elapsed'

                    while (true) {
                        long iterStart = 0L
                        long iterDuration = 0L
                        int rc = 0
                        boolean stop = false

                        // The whole iteration body is inside the try, not just the build:
                        // `date`, the metrics probe and the df read are `sh` steps too, and
                        // the failure this handler exists for (workspace gone, dropped
                        // docker exec) would otherwise surface from one of them and go red
                        // past it.
                        try {
                            long now = sh(returnStdout: true, script: 'date +%s').trim().toLong()

                            if (now >= deadline) {
                                stop = true
                            } else if (iteration >= MAX_ITERATIONS) {
                                // Bounds flow-graph growth in program.dat on the shared
                                // controller. Stops only - reaching it is not by itself a
                                // bad run: "one loaded iteration, one instant failure,
                                // repeat" can hit the cap with the node loaded throughout,
                                // and the suite's own failures are irrelevant to a job that
                                // is a resource consumer rather than a test.
                                stopReason = "hit the ${MAX_ITERATIONS}-iteration cap"
                                echo "[LOADGEN] ${stopReason} - stopping"
                                stop = true
                            } else if (iteration > 0 && maxLoadedDuration > 0 && (now + maxLoadedDuration) > deadline) {
                                // The deadline is only checked between iterations, so
                                // without this the overrun would be a whole suite run.
                                // The first iteration always runs, so MINUTES=1 still means
                                // exactly one full suite rather than none - and we never
                                // kill a suite mid-flight, which would leave stray QEMUs and
                                // a half-written qemu_flash.bin to poison the next round.
                                stopReason = "stopped early: ${deadline - now}s left but a loaded iteration takes up to ${maxLoadedDuration}s"
                                echo "[LOADGEN] ${stopReason}"
                                stop = true
                            }

                            if (!stop) {
                                // Disk is the one shared resource whose exhaustion would
                                // break the measured run rather than slow it: an ENOSPC
                                // inside its `make qemu-test` surfaces as a firmware
                                // failure. This is not a validity check - it only stops
                                // the loop; coverage still decides whether the run counts.
                                //
                                // df is captured, not piped into awk, for the same reason
                                // make is above: on the left of a pipe its exit status is
                                // lost, awk returns 0 on empty input, and the floor would
                                // then be silently unenforced for the whole run. If the
                                // probe still yields nothing usable, say so in the log -
                                // an unenforced guard must never be invisible.
                                String freeRaw = sh(returnStdout: true, script: """
set -e
OUT="\$(df -Pk .)"
echo "\$OUT" | awk 'NR==2 {print \$4}'
""").trim()
                                long freeKb = freeRaw.isLong() ? freeRaw.toLong() : -1L
                                if (freeKb < 0L) {
                                    echo "[LOADGEN] WARNING: free-space probe returned '${freeRaw}' - the " +
                                         "${DISK_FLOOR_KB} KiB floor is NOT enforced this iteration"
                                } else if (freeKb < DISK_FLOOR_KB) {
                                    diskLow = true
                                    stopReason = "free disk fell to ${freeKb} KiB, below the ${DISK_FLOOR_KB} KiB floor"
                                    echo "[LOADGEN] WARNING: ${stopReason} - stopping so the measured run does not hit ENOSPC"
                                    stop = true
                                }
                            }

                            if (!stop) {
                                iteration++
                                iterStart = now
                                echo "[LOADGEN] iteration ${iteration} starting - elapsed ${now - startEpoch}s of ${minutes * 60}s"
                                echo identity
                                sh metricsCmd

                                // returnStatus keeps a failing suite from throwing. This
                                // suite is a resource consumer, not a test: its verdict says
                                // nothing about the product, and under the contention we are
                                // creating on purpose it is expected to go red. Failing the
                                // build here would read as a product regression that did not
                                // happen.
                                rc = sh(
                                    returnStatus: true,
                                    script: '''bash -c 'source /opt/esp/idf/export.sh && make qemu-test' '''
                                )
                                iterDuration = sh(returnStdout: true, script: 'date +%s').trim().toLong() - iterStart
                            }
                        } catch (org.jenkinsci.plugins.workflow.steps.FlowInterruptedException interrupt) {
                            // A manual abort or the pipeline timeout. Must propagate:
                            // swallowing it would keep the node loaded after someone
                            // explicitly asked the job to stop.
                            throw interrupt
                        } catch (hudson.AbortException abort) {
                            // A step could not RUN (workspace gone, dropped docker exec).
                            // A red suite never reaches here - it returns a status instead -
                            // and no error() is raised inside this try, so this handler
                            // cannot catch a deliberate failure of ours. Breaking here skips
                            // classification, which is precisely why the coverage check
                            // after the loop, not the streak counter, is the real guard.
                            stepFailed = true
                            stopReason = "a pipeline step could not run (${abort.message})"
                            echo "[LOADGEN] iteration ${iteration} could not run (${abort.message}) - ending the loop"
                        }

                        if (stop || stepFailed) {
                            break
                        }

                        // CLASSIFICATION - deliberately OUTSIDE the try, so nothing below
                        // can be swallowed by the catch above.
                        //
                        // Duration first, exit code second. A suite that "passes" in two
                        // seconds loaded the node exactly as little as one that fails in
                        // two seconds, so resetting the streak on rc == 0 alone would let
                        // an alternating fast-pass/fast-fail pattern spin forever.
                        if (iterDuration < LOAD_FLOOR_SECONDS) {
                            noLoadStreak++
                            echo "[LOADGEN] iteration ${iteration} produced NO MEANINGFUL LOAD " +
                                 "(exit ${rc} after ${iterDuration}s, floor is ${LOAD_FLOOR_SECONDS}s) " +
                                 "- ${noLoadStreak}/${NO_LOAD_LIMIT} consecutive"
                            if (noLoadStreak >= NO_LOAD_LIMIT) {
                                // Stop early, but do NOT condemn the run: a window that was
                                // loaded for most of its length and then degraded into fast
                                // failures has measured exactly what this experiment exists
                                // to observe. Only coverage decides.
                                streakStop = true
                                stopReason = "${noLoadStreak} consecutive iterations shorter than ${LOAD_FLOOR_SECONDS}s"
                                echo "[LOADGEN] ${stopReason} - stopping early; the coverage line below decides " +
                                     "whether this run counts"
                                break
                            }
                        } else {
                            noLoadStreak = 0
                            loadedIterations++
                            loadedSeconds += iterDuration
                            if (iterDuration > maxLoadedDuration) {
                                maxLoadedDuration = iterDuration
                            }
                            echo "[LOADGEN] iteration ${iteration} loaded the node for ${iterDuration}s (suite exit ${rc}, " +
                                 "ignored on purpose)"
                        }
                    }

                    long finished = sh(returnStdout: true, script: 'date +%s').trim().toLong()
                    echo "[LOADGEN] finished after ${iteration} iteration(s), ${finished - startEpoch}s wall clock, " +
                         "reason: ${stopReason}"

                    // THE decision. Printed prominently first, because this line is what
                    // makes the run interpretable: it says how much of the requested window
                    // was genuinely under load. Over 100% is normal (the final iteration
                    // overruns) and is not an error.
                    long requested = minutes * 60L
                    int pct = requested > 0 ? (int) (100L * loadedSeconds / requested) : 0
                    echo "[LOADGEN] COVERAGE: loaded ${loadedIterations}/${iteration} iterations, " +
                         "${loadedSeconds}s of ${requested}s requested (${pct}%)"

                    echo identity
                    echo '[LOADGEN] final measurement after load'
                    sh metricsCmd

                    String endedWith = "The loop ended because: ${stopReason}." +
                        (stepFailed ? ' An infrastructure failure, not a suite failure - the node or workspace went away.' : '') +
                        (diskLow ? ' Disk space on the node ran low; free it before re-running.' : '') +
                        (streakStop ? ' The suite was failing fast by the end - read its output above; a check-idf-pins' +
                                      ' mismatch, a port conflict, a conftest ImportError from a bad slot, or a compile' +
                                      ' error are the usual causes.' : '')

                    // THE verdict, and the only place `invalid` is ever set. Declared here
                    // rather than before the loop so that it structurally cannot be reached
                    // from inside it - error() throws hudson.AbortException, which the catch
                    // in the loop would swallow, turning "this measurement is invalid" into
                    // a green build with a misleading "could not run" line.
                    boolean invalid = false
                    String invalidReason = ''

                    if (loadedSeconds == 0L) {
                        invalid = true
                        invalidReason = "no iteration ever reached the ${LOAD_FLOOR_SECONDS}s load floor, so this " +
                            "build placed NO meaningful load on ${env.NODE_NAME}. Any e2e result measured against " +
                            "it is INVALID - it was measured against an idle node. ${endedWith}"
                    } else if (pct < MIN_COVERAGE_PCT) {
                        invalid = true
                        invalidReason = "only ${pct}% of the requested ${requested}s window was actually under load " +
                            "(${loadedSeconds}s across ${loadedIterations} iteration(s)), below the " +
                            "${MIN_COVERAGE_PCT}% minimum. The measured run spent most of the window against an " +
                            "idle node, so treat its result as INVALID. ${endedWith}"
                    }

                    // Raised here, outside every try in this script, so it actually reaches
                    // Jenkins and turns the build red.
                    if (invalid) {
                        error(invalidReason)
                    }
                }
            }
            post {
                // Stage-level, because a pipeline-level post block cannot run under
                // `agent none` - there would be no workspace to delete. `cleanup` runs
                // whatever the stage result, including the invalid-measurement failure.
                cleanup {
                    deleteDir()
                }
            }
        }
    }
}
