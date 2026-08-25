pipeline {
    agent {
        dockerfile {
            reuseNode true
            label 'devenv'
            args '--entrypoint=""'
        }
    }
    options {
        copyArtifactPermission('/s3_uploader');
        // Serialise a branch's builds against each other. This is about the QEMU e2e suite,
        // not tidiness: that suite is CPU-starvation sensitive, and under contention its
        // data-path tests get exactly zero bytes back (sent=..., got='' — never a partial
        // read, and the firmware does not crash). Two builds of the same branch running the
        // e2e stage at the same time on the same node ARE that contention, self-inflicted,
        // and it has been observed live. Measured elsewhere: ~1.5x CPU oversubscription is
        // already enough to reproduce the cascade, while with no contention the same suite
        // runs green. Removing this to drain the queue faster brings the flakiness back.
        //
        // CPU is the whole of the rationale. Host-port collisions between concurrent runs are
        // NOT part of it, and neither half of the suite's port allocation can produce one.
        // Every host port that is fixed in advance — the QEMU hostfwd targets, the two UART
        // chardev TCP ports, and the UDP IO-bus port — is derived from WB_MGE_PORT_SLOT (see
        // the two QEMU stages and api_tests/qemu_ports.py), so concurrent runs in different
        // slots already coexist on separate ports. Grepping api_tests/ for `.bind(` finds four
        // calls, and none of them adds a collision. Three are the listeners the tests own
        // themselves (25_test_transparent_tcp_e2e.py, and 32_test_transparent_sniffer.py
        // twice): they bind port 0, so the OS hands each a free ephemeral port and they cannot
        // collide by construction. The fourth is the stale-port preflight probe in
        // conftest.py, which binds the slot-derived UDP IO-bus port — already covered by the
        // slot argument above. (The IO-bus client socket in io_bus_helpers.py never binds at
        // all; its sendto takes an implicit ephemeral source port.)
        //
        // Residual limitation, deliberately not solved here: disableConcurrentBuilds is a
        // per-branch job property, so it only stops a branch from doubling up on ITSELF and
        // never makes one branch wait for another. The node is shared with other jobs, and
        // nothing in this file isolates the suite from them. The fixes for that (a dedicated
        // executor, a lock() from the Lockable Resources plugin, or cpuset pinning) are
        // owner-level CI decisions, and the installed plugin set could not be confirmed from
        // here.
        disableConcurrentBuilds();
    }
    triggers {
        // Nightly coverage, without a second Jenkins job. parameterizedCron starts a build of
        // THIS job with these parameters already set, which is exactly what a separate trigger
        // job would have done — minus the hand-made multibranch project it would have needed.
        //
        // 'main' only, and the empty string is how every other branch says "no schedule". That is
        // the plugin's actual behaviour, not an assumption: ParameterizedCronTabList.create()
        // splits the spec on newlines and skips blank lines, so '' yields a list of zero crontabs
        // and nothing throws; the plugin's scheduler (an AperiodicWork that runs OUTSIDE any
        // build) then finds no crontab to fire. A branch that schedules nothing cannot break its
        // own builds through this block.
        //
        // env.BRANCH_NAME resolves here because Declarative evaluates a trigger's arguments at
        // runtime in the script context, not at parse time. The one documented exception is
        // variables from an 'environment' block, which are applied later via withEnv; BRANCH_NAME
        // is not one of those — it comes from the multibranch job itself and is set before the
        // pipeline body runs.
        //
        // Like 'parameters', a 'triggers' block only takes effect from the SECOND build after it
        // appears: Jenkins has to run the pipeline once to learn that the trigger exists. The
        // first build of main after this lands therefore schedules nothing, and the one after it
        // does. Nothing else has to be created or configured by hand.
        //
        // PRECONDITION, and it is not a core one: parameterizedCron comes from the Parameterized
        // Scheduler plugin. Its presence WAS verified on this server — the declarative linter at
        // {JENKINS}/pipeline-model-converter/validate enumerates parameterizedCron among the valid
        // trigger types, and rejects a bogus trigger name by printing exactly that enumeration. If
        // the plugin is ever uninstalled the failure is loud rather than silent: Declarative fails
        // at PARSE time, so every build of every branch and every PR breaks at once. What cannot
        // happen is the nightly quietly disappearing while everything else keeps building.
        //
        // Firing every night is not the same as re-measuring every night, and not the same as
        // BUILDING every night either. The 'Coverage precheck' stage below
        // decides whether this exact commit already has a combined report; on a match it sets
        // NIGHTLY_COVERAGE_SKIP, and EVERY other stage in this file carries that flag as a
        // conjunct in its 'when'. Lints, unit tests, frontend, firmware build, clang-tidy, the
        // coverage stages and 'Upload to S3' alike are then skipped, and the build ends green with
        // a description saying why — see that stage for why it must not end NOT_BUILT.
        //
        // Be honest about the baseline. The design this replaced was a separate pollSCM job, and
        // on a day with no pushes it built NOTHING: zero node time, zero s3_uploader invocations.
        // A plain cron cannot reproduce that, because it always starts a build. What that build
        // costs on a quiet night is its fixed overhead and nothing else — an executor on 'devenv',
        // the implicit checkout, a cached image build, a container start, and the precheck's
        // bounded walk over previous build records. No sh step runs, nothing is compiled or
        // tested, and s3_uploader is not invoked.
        // (An earlier version of this comment argued that a quiet night costs "an ordinary build
        // rather than an ordinary build plus ~2 h of coverage". That measured against the wrong
        // baseline — the baseline is zero — and it described the state before the whole-file
        // guard existed, which was ~365 extra full builds and ~365 extra s3_uploader invocations
        // a year where the replaced design ran none.)
        parameterizedCron(env.BRANCH_NAME == 'main' ? 'H 2 * * * %RUN_COVERAGE=true;RUN_E2E=false' : '')
    }
    parameters {
        booleanParam(name: 'UPLOAD_FROM_BRANCH', description: 'Upload results to S3 even if it is not master branch', defaultValue: false)
        // Coverage is off by default on purpose, and permanently: an ordinary build is meant to
        // run the e2e suite ONCE, and 'Coverage (QEMU e2e)' re-runs the whole suite on an
        // instrumented build, so switching this on doubles a ~2 h build. This default is the
        // policy, not a stopgap.
        //
        // What produces coverage instead is the parameterizedCron trigger declared above in this
        // same file, which starts a build with RUN_COVERAGE=true, RUN_E2E=false. Do not read that
        // as "this branch gets nightly coverage" — its reach is narrower than it sounds:
        //   * 'main' only. The cron spec is that ternary's empty string on every other branch,
        //     which the scheduler reads as "no crontab at all". This Jenkinsfile is read on every
        //     branch; the schedule it declares exists on one.
        //   * Once a night, and it MEASURES only when the commit has not been measured yet. A
        //     plain cron fires whether or not anything was pushed, so the check lives in the
        //     'Coverage precheck' stage below: it compares this commit against the one the last
        //     coverage build recorded in its build description, and on a match skips not just the
        //     coverage stages but every other stage in this file. Every uncertainty there
        //     resolves to "run coverage".
        //   * Only from the second build after the trigger appeared, since Jenkins learns about a
        //     'triggers' block by running the pipeline once — the same first-build rule that
        //     applies to the 'parameters' block below.
        // So outside 'main' the only coverage that runs is coverage somebody asks for by setting
        // this parameter.
        //
        // The two parameters are INDEPENDENT gates, which is exactly what makes a coverage-only
        // build possible:
        //   RUN_COVERAGE=false, RUN_E2E=true  — ordinary build: the e2e suite runs once, ~2 h;
        //   RUN_COVERAGE=true,  RUN_E2E=false — nightly coverage build: unit coverage + QEMU
        //                                       coverage + combined report, ~2 h, no separate
        //                                       plain e2e run. Fired by the parameterizedCron
        //                                       trigger above.
        // Setting both on runs the suite twice (~4 h) and is only worth it for a one-off.
        // These two describe only which of the COVERAGE and E2E stages run. The rest of the
        // pipeline — lints, unit tests, frontend, firmware build, clang-tidy and 'Upload to S3' —
        // runs on any build that is not a nightly the precheck decided to skip, which means a
        // coverage-only nightly that DOES measure still re-invokes s3_uploader for the same
        // revision the day's push build already handed to it. See that stage for what is and is
        // not known about the consequences. A nightly the precheck skips runs none of them.
        booleanParam(name: 'RUN_COVERAGE', description: 'Run the coverage stages: unit tests and QEMU e2e. The combined report additionally requires both of those to succeed. Independent of RUN_E2E. Off by default: coverage runs when it is explicitly requested here, or once a night on main via the parameterizedCron trigger in this Jenkinsfile. A nightly that finds a combined report already recorded for the very same commit skips every stage in the pipeline except that check itself; ticking this box by hand always runs them', defaultValue: false)
        booleanParam(name: 'RUN_E2E', description: 'Run the plain QEMU e2e API suite and publish its junit. On by default, and meant to stay on: the suite is expected to run on every build. Does not gate the coverage stages — with RUN_COVERAGE on, the QEMU coverage stage re-runs the suite regardless', defaultValue: true)
    }

    stages {
        stage('Coverage precheck') {
            // Answers one question for the nightly: has coverage already been produced for THIS
            // commit? The trigger above is a plain cron — it fires whether or not anything was
            // pushed — so without this stage a quiet week would re-measure the same commit every
            // night, ~2 h at a time, for byte-identical numbers.
            //
            // The verdict controls the WHOLE build, not merely the coverage stages.
            // NIGHTLY_COVERAGE_SKIP appears as a conjunct in every other stage's 'when' in this
            // file, so a skipped nightly runs no lint, no test, no build and no s3_uploader — see
            // the trigger comment at the top for the cost that leaves and why the alternative was
            // ~365 pointless full builds a year. This stage is the one stage deliberately NOT
            // guarded by the flag: it is what sets it.
            //
            // The changeset cannot answer it. This job builds every push, so by 02:00 the nightly
            // build sits on a commit whose build already happened and its changeset is empty every
            // single time; a changeset check would skip coverage permanently. What is compared
            // instead is the commit that coverage last RAN on, recorded in the build description
            // by 'Coverage (combined)' below and read back here by walking previous builds. A
            // nightly that skips writes a description saying so, but never a marker: the marker it
            // matched stays the most recent one, so the nights after it match it too. That is only
            // safe because the marker is an anchored token (see coverageMarkerOpen below) that no
            // prose can spell by accident — including the skip note this stage now writes, which
            // contains the word "coverage" and would have been read back as a marker under the
            // bare-word scheme this replaced.
            //
            // Everything here fails OPEN, i.e. leaves NIGHTLY_COVERAGE_SKIP unset and lets the
            // build run in full: no previous build, a null or unparseable description, a missing
            // GIT_COMMIT, a marker older than the walk limit, or any of the tolerated exceptions
            // below. Re-running coverage costs ~2 h once; a guard that wrongly skips is a nightly
            // that quietly never measures anything again, and nothing would report it.
            when { expression { params.RUN_COVERAGE == true } }
            steps {
                script {
                    // The marker written by 'Coverage (combined)', and how far back to look for
                    // it. The walk is bounded because it loads one build record per hop; 100 is
                    // several weeks of this job at its usual rate, and overshooting the limit
                    // only means coverage runs again.
                    //
                    // The marker is the bracketed, punctuated token '[coverage-commit:<sha>]',
                    // not the bare word it used to be. A bare 'coverage ' made every description
                    // containing that word a candidate marker, which fails open but also means
                    // the word could never be written into a description by hand — including, and
                    // this stage now does exactly that, to label a skipped nightly. Keep the two
                    // literals below in sync with the writer in 'Coverage (combined)'; they are
                    // the whole contract between the two stages.
                    String coverageMarkerOpen = '[coverage-commit:'
                    String coverageMarkerClose = ']'
                    int maxHops = 100

                    String outcome = 'coverage will run'
                    try {
                        // Only the timer gets to skip. Somebody who ticks RUN_COVERAGE by hand
                        // means it, even on a commit that already has a report — a rerun is
                        // exactly what "run it again" asks for.
                        //
                        // getBuildCauses() renders the causes as JSON objects. Matching class
                        // names by SUBSTRING rather than filtering by an exact one is deliberate:
                        // two cause classes are in play — hudson.triggers.TimerTrigger$
                        // TimerTriggerCause and the parameterized scheduler's
                        // ParameterizedTimerTriggerCause, which extends it — and
                        // getBuildCauses(String) compares class names with equals(), so it would
                        // match only whichever one was named. The shortDescription arm ("Started
                        // by timer", "Started by timer with parameters: ...") is a second way to
                        // recognise the same thing.
                        //
                        // What the substring must NOT be applied to is the whole rendered cause.
                        // hudson.model.Cause$UpstreamCause exports a nested 'upstreamCauses'
                        // list, so a build started by ANOTHER job that a timer had started would
                        // render text containing 'TimerTriggerCause' and wrongly qualify for the
                        // skip. Only each cause's own top-level fields are read here — '_class'
                        // and 'shortDescription' — never the blob, so a nested cause cannot vote.
                        // No upstream job invokes this one today, so that was latent rather than
                        // live; the fix costs two map lookups.
                        boolean startedByTimer = false
                        for (Object cause : currentBuild.getBuildCauses()) {
                            Map causeFields = (cause instanceof Map) ? cause : [:]
                            String causeClass = causeFields.get('_class')?.toString() ?: ''
                            String causeText = causeFields.get('shortDescription')?.toString() ?: ''
                            if (causeClass.contains('TimerTriggerCause') || causeText.startsWith('Started by timer')) {
                                startedByTimer = true
                                break
                            }
                        }

                        // Cheap checks first, then the walk. The two conditions below are a field
                        // read apiece, while the walk loads up to maxHops build records from disk,
                        // one lazy Run per hop — and its answer is usable only for a timer-started
                        // build that knows its own commit. Anything else discards it, so it is not
                        // computed at all. The outcomes are exactly the ones the flat chain
                        // produced; only the order of evaluation changed.
                        String thisCommit = env.GIT_COMMIT
                        if (!startedByTimer) {
                            outcome = 'not started by the timer, so coverage runs as asked'
                        } else if (!thisCommit) {
                            outcome = 'GIT_COMMIT is not set, so this build cannot be matched against a previous one — coverage will run'
                        } else {
                            String lastCoveredCommit = null
                            def previous = currentBuild.previousBuild
                            int hops = 0
                            while (previous != null && hops < maxHops) {
                                String description = previous.description
                                if (description) {
                                    // lastIndexOf, because 'Coverage (combined)' APPENDS its marker
                                    // to whatever description the build already carried: the marker
                                    // is the last occurrence by construction. Text on either side
                                    // of the token is tolerated — the value is whatever sits between
                                    // the token and the next ']'. A description with no well-formed
                                    // marker (no token, or a token with no closing bracket) is
                                    // walked PAST rather than treated as an answer, and a
                                    // well-formed token holding something that is not this SHA
                                    // compares unequal and lets coverage run.
                                    int markerStart = description.lastIndexOf(coverageMarkerOpen)
                                    if (markerStart >= 0) {
                                        int valueStart = markerStart + coverageMarkerOpen.length()
                                        int valueEnd = description.indexOf(coverageMarkerClose, valueStart)
                                        if (valueEnd > valueStart) {
                                            lastCoveredCommit = description.substring(valueStart, valueEnd).trim()
                                            break
                                        }
                                    }
                                }
                                previous = previous.previousBuild
                                hops++
                            }

                            if (lastCoveredCommit == thisCommit) {
                                // Build the message FIRST and set the flag LAST. Anything that
                                // throws in this branch then leaves NIGHTLY_COVERAGE_SKIP unset,
                                // which is the fail-open promise at the top of this stage: the
                                // catch clauses below rewrite 'outcome' into "coverage will run",
                                // and with the flag already set that log line would have been a
                                // lie while the whole build stayed skipped.
                                outcome = "commit ${thisCommit} already has a combined coverage report — skipping every stage in this build"
                                env.NIGHTLY_COVERAGE_SKIP = 'true'
                            } else if (lastCoveredCommit) {
                                outcome = "the last combined coverage was for ${lastCoveredCommit}, not ${thisCommit} — coverage will run"
                            } else {
                                outcome = "no combined coverage recorded in the last ${hops} build(s) — coverage will run"
                            }
                        }
                    } catch (IOException e) {
                        // Fail open, loudly. The message names the exception so a permanently
                        // broken precheck is visible in the log rather than merely expensive.
                        //
                        // These three types are what this block actually wants to tolerate, and
                        // NOT 'Exception'. currentBuild.getBuildCauses() declares BOTH IOException
                        // and ClassNotFoundException, so both need a clause of their own — this
                        // stage is wrapped in no catchError, so a ClassNotFoundException escaping
                        // here would fail the whole build, which is the exact opposite of the
                        // fail-open promise above. hudson.AbortException is a subclass of
                        // IOException and arrives through this clause; RuntimeException covers
                        // everything the walk and the parsing can raise — a null, a bad index, a
                        // sandbox rejection. Catching 'Exception' would also catch
                        // FlowInterruptedException, which extends InterruptedException and
                        // therefore Exception: a build cancelled while this stage ran would have
                        // its cancellation rewritten into "precheck failed" and reported as a
                        // completed build instead of ABORTED. None of the three clauses here can
                        // match it, so an interruption propagates and Jenkins reports it as what
                        // it is.
                        outcome = "precheck failed (${e}) — coverage will run"
                    } catch (ClassNotFoundException e) {
                        // Same failure-open behaviour; see the type note on the clause above.
                        outcome = "precheck failed (${e}) — coverage will run"
                    } catch (RuntimeException e) {
                        // Same failure-open behaviour; see the type note on the clause above.
                        outcome = "precheck failed (${e}) — coverage will run"
                    }
                    echo "Coverage precheck: ${outcome}"

                    if (env.NIGHTLY_COVERAGE_SKIP == 'true') {
                        // Make the skip VISIBLE in the build list. Without this, a nightly that
                        // did nothing is a green ball indistinguishable from one that did the
                        // whole build, and the only trace is the console line just above — thin
                        // evidence for a mechanism whose failure mode is "the nightly silently
                        // stopped measuring anything". The description below IS that visibility,
                        // and it is all of it: the build keeps its default result.
                        //
                        // Do NOT set currentBuild.result = 'NOT_BUILT' here, however grey and
                        // harmless it looks. This job is a multibranch project on GitHubSCMSource
                        // (confirmed in the live job's config.xml), and github-branch-source reports
                        // every finished build back to the commit: its
                        // GitHubNotificationContext.getDefaultState() maps any result that is not
                        // SUCCESS or UNSTABLE — NOT_BUILT included — to GHCommitState.ERROR. The
                        // nightly runs on the SAME commit the daytime push build already reported
                        // SUCCESS for, so the identical (sha, context) pair would be overwritten
                        // with ERROR: the green tick on the head of main disappears every quiet
                        // night, and branch protection, release automation and anyone reading the
                        // commit list see a broken main. Second reason, if the first is not enough:
                        // NOT_BUILT is not the mild result it looks like — Run.setResult accepts
                        // only a WORSE result and NOT_BUILT ranks worse than FAILURE, so it would
                        // have masked a later failure rather than yielded to it.
                        //
                        // Leaving the build green is honest rather than a fudge: nothing ran and
                        // nothing failed.
                        //
                        // The description says why, in prose, and is deliberately NOT a marker: it
                        // carries no '[coverage-commit:' token, so the next nightly's walk steps
                        // over it and finds the real marker behind it. Writing the word "coverage"
                        // into a description like this is precisely what the old bare-word marker
                        // made unsafe. Appended rather than assigned over, like the writer in
                        // 'Coverage (combined)'.
                        //
                        // Wrapped, and narrowly: setting a description writes the build record, so
                        // it can fail with IOException, and that is not worth turning a correct
                        // skip into a red build. FlowInterruptedException is out of scope here for
                        // the same reason as above.
                        try {
                            String existing = currentBuild.description
                            String note = "nightly skipped: coverage already recorded for ${env.GIT_COMMIT}"
                            currentBuild.description = existing ? "${existing} ${note}" : note
                        } catch (IOException e) {
                            echo "Could not mark the skipped nightly (${e})"
                        } catch (RuntimeException e) {
                            echo "Could not mark the skipped nightly (${e})"
                        }
                    }
                }
            }
        }
        stage('Cleanup workspace') {
            // Every stage from here down carries this same conjunct, so that a nightly the
            // precheck resolved as "already covered" runs nothing at all. What the guard saves on
            // THIS stage is small: its two steps are an 'idf.py fullclean', a pile of 'rm -rf'
            // and a 'make clean' per unit-test dir (see the 'clean' target in Makefile), seconds
            // of work next to any stage that compiles or runs tests. In particular it does not
            // spare the NEXT real build a full rebuild — this is the first stage every
            // non-skipped build runs after the precheck, so that build begins by running
            // 'make clean' itself whatever the skipped nightly did before it. The
            // guard is written here for uniformity with all the other stages; the hours are saved
            // by the expensive ones below — 'Unit tests (C)', 'Build firmware',
            // 'Lint C (clang-tidy)' and the e2e and coverage stages. See 'Coverage precheck'.
            when { expression { env.NIGHTLY_COVERAGE_SKIP != 'true' } }
            steps {
                script {
                    sh 'bash -c "source /opt/esp/idf/export.sh && make clean"'
                    sh 'rm -rf result/'
                }
            }
        }
        stage('Lint frontend (ESLint)') {
            when { expression { env.NIGHTLY_COVERAGE_SKIP != 'true' } }
            steps {
                // catchError keeps build UNSTABLE (yellow) on lint failure — subsequent stages still run
                catchError(buildResult: 'UNSTABLE', stageResult: 'FAILURE') {
                    sh 'bash -c "make lint-frontend"'
                }
            }
            post {
                always {
                    junit testResults: 'build/eslint_report.xml', allowEmptyResults: true
                    archiveArtifacts artifacts: "build/eslint_report.xml", allowEmptyArchive: true
                }
            }
        }
        stage('Lint comments') {
            when { expression { env.NIGHTLY_COVERAGE_SKIP != 'true' } }
            steps {
                catchError(buildResult: 'UNSTABLE', stageResult: 'FAILURE') {
                    sh 'make lint-comments'
                }
            }
        }
        stage('Unit tests (C)') {
            when { expression { env.NIGHTLY_COVERAGE_SKIP != 'true' } }
            steps {
                script {
                    sh 'bash -c "source /opt/esp/idf/export.sh && make -j unittests"'
                }
            }
            post {
                always {
                    // Aggregate Unity stdout logs into a JUnit XML; run even on test failure
                    sh 'python3 scripts/unity_to_junit.py --output build/unittests_report.xml --logs unittests || true'
                    junit testResults: 'build/unittests_report.xml', allowEmptyResults: true
                    archiveArtifacts artifacts: "build/unittests_report.xml", allowEmptyArchive: true
                }
            }
        }
        stage('Unit tests (frontend)') {
            when { expression { env.NIGHTLY_COVERAGE_SKIP != 'true' } }
            steps {
                script {
                    sh 'make test-frontend'
                }
            }
            post {
                always {
                    junit testResults: 'build/vitest_report.xml', allowEmptyResults: true
                    archiveArtifacts artifacts: "build/vitest_report.xml", allowEmptyArchive: true
                }
            }
        }
        stage('Build frontend') {
            when { expression { env.NIGHTLY_COVERAGE_SKIP != 'true' } }
            steps {
                script {
                    sh 'make build-frontend'
                }
            }
        }
        stage('Build firmware') {
            when { expression { env.NIGHTLY_COVERAGE_SKIP != 'true' } }
            steps {
                script {
                    sh 'bash -c "source /opt/esp/idf/export.sh && make build-idf-project"'
                    // copy binaries to separate 'result' directory because s3_uploader job searches for files there
                    sh 'mkdir -p result && cp release/*.bin result/'
                }
            }
            post {
                success {
                    archiveArtifacts artifacts: "result/*.bin"
                }
            }
        }
        stage('Lint C (clang-tidy)') {
            when { expression { env.NIGHTLY_COVERAGE_SKIP != 'true' } }
            steps {
                // catchError keeps build UNSTABLE (yellow) on lint findings — S3 Upload still runs
                catchError(buildResult: 'UNSTABLE', stageResult: 'FAILURE') {
                    sh 'bash -c "source /opt/esp/idf/export.sh && make lint-c"'
                }
            }
            post {
                always {
                    // wb_clang_tidy.py writes findings to /tmp/clang-tidy-out and logs to
                    // /tmp/clang-tidy-log — copy into workspace so they survive as artifacts
                    sh '''
                        mkdir -p build/clang-tidy
                        cp -r /tmp/clang-tidy-out /tmp/clang-tidy-log build/clang-tidy/ 2>/dev/null || true
                    '''
                    archiveArtifacts artifacts: "build/clang-tidy/**", allowEmptyArchive: true
                }
            }
        }
        stage('E2E tests (QEMU)') {
            // This suite REPORTS, it does not gate. The catchError below downgrades a failure
            // to UNSTABLE (yellow), so a red suite here never fails the build and never blocks
            // a merge — read it as a signal to investigate, not as a verdict.
            //
            // Gating is the goal, and the deferral is deliberate rather than lazy. The suite is
            // starvation-sensitive (see the disableConcurrentBuilds comment in options): on a
            // busy node its data-path tests read back zero bytes and the run goes red without
            // any regression having happened. A gate that fires mostly because the node was
            // busy is one people learn to click past, and a gate nobody believes is worse than
            // no gate — it also devalues the red builds that are real.
            //
            // Criterion for flipping this to gating: the suite green for 10 consecutive builds
            // on a node where it is not competing for CPU — fewer than that cannot tell a fixed
            // suite from a lucky one. Where the streak currently stands is a question for the
            // job's build history, deliberately not recorded here: a tally written into a
            // comment is stale the next build. When the criterion holds, flip it by deleting
            // the catchError wrapper below and leaving the post block as is.

            // Host-port isolation for the suite. Every host port it touches (QEMU hostfwd
            // targets, the two UART chardevs, the UDP IO bus) is derived from this one
            // integer by api_tests/qemu_ports.py, so two builds that land on the same node
            // no longer fight over 8080/50502-4/5561-2.
            //
            // EXECUTOR_NUMBER is Jenkins' per-executor index on the node, which is exactly
            // the granularity wanted: one concurrent build per executor, so distinct
            // concurrent builds get distinct blocks. This is CROSS-JOB isolation —
            // disableConcurrentBuilds() in options already serialises a branch against
            // ITSELF, and the residual exposure named in that comment is the other jobs
            // sharing the node. It does not fix CPU contention, which is a separate and
            // unsolved problem; it fixes the part where two suites also collided on ports.
            //
            // Set here rather than left to qemu_ports' own EXECUTOR_NUMBER fallback: this
            // resolves EXECUTOR_NUMBER on the Jenkins side, where it is certainly defined,
            // instead of assuming it survives into the container's shell environment. The
            // fallback stays as a backstop for e2e runs started outside these stages.
            // '?: 0' guards the null EXECUTOR_NUMBER; an empty value would resolve to slot 0
            // too (qemu_ports treats an empty WB_MGE_PORT_SLOT as unset rather than raising).
            //
            // Ports are only half of it: the OTHER half is that each run needs its own
            // WORKING TREE, which Jenkins gives us for free (one workspace per job) and
            // which is now enforced with an flock on .e2e-tree.lock in the workspace root
            // — taken by `make qemu-test` around its whole recipe (api_tests/tree_lock.py),
            // so it covers the firmware build too, and re-checked inside pytest.
            environment { WB_MGE_PORT_SLOT = "${env.EXECUTOR_NUMBER ?: 0}" }
            // '!= false' is load-bearing for the case where params.RUN_E2E is NULL. Be precise
            // about when that actually happens: params.X reads the job's registered
            // ParametersDefinitionProperty, which a Jenkinsfile only updates by running once. So
            // null means the parameter is not registered AT ALL — a branch Jenkins has never
            // built, or a parameter newly added to this file. It is NOT what you get from merely
            // changing an existing parameter's default: that build still sees the parameter,
            // holding its OLD default, and only the run after picks up the new one. This
            // parameter defaults to ON, so null must read as "enabled"; in Groovy 'null != false'
            // is true, so the stage still runs. RUN_COVERAGE below defaults OFF and uses the
            // mirror-image '== true' for the same reason — the two idioms differ on purpose.
            // The NIGHTLY_COVERAGE_SKIP conjunct is the whole-file guard; see 'Coverage precheck'.
            when { expression { params.RUN_E2E != false && env.NIGHTLY_COVERAGE_SKIP != 'true' } }
            steps {
                // catchError keeps build UNSTABLE (yellow) on e2e failure — S3 Upload still runs
                catchError(buildResult: 'UNSTABLE', stageResult: 'FAILURE') {
                    // The resolved slot and its port block are printed in the pytest report
                    // header, so the build log answers "did this actually take effect?"
                    sh 'bash -c "source /opt/esp/idf/export.sh && make qemu-test"'
                }
            }
            post {
                always {
                    // Publish JUnit results so failed tests appear in Jenkins "Tests" tab
                    junit testResults: 'build/qemu_test_report.xml', allowEmptyResults: true
                    // Archive QEMU log and JUnit XML for debugging regardless of test outcome
                    archiveArtifacts artifacts: "build/qemu_test.log,build/qemu_test_report.xml", allowEmptyArchive: true
                }
            }
        }
        // Coverage stages run before the S3 upload; catchError keeps the build UNSTABLE
        // (yellow) on coverage failure so the firmware upload still runs. All three are gated on
        // RUN_COVERAGE and nothing else — the QEMU one used to additionally require RUN_E2E,
        // which made "coverage without a second e2e run" unreachable; see that stage for why
        // that extra gate went away. When the clean e2e stage runs at all, these run after it.
        // '== true' is deliberate, and it flipped together with the default. It covers the null
        // case — params.RUN_COVERAGE is null when the parameter is not registered on the job at
        // all (a branch Jenkins has never built, or a parameter newly added to this file), NOT
        // simply because a default changed; see the fuller note on the e2e stage above. The
        // default is now OFF, so null has to read as "disabled" — in Groovy 'null == true' is
        // false, so the stage is skipped. (It was '!= false' while the default was ON, for the
        // mirror-image reason.)
        // The NIGHTLY_COVERAGE_SKIP arm is the 'Coverage precheck' verdict: set only when the
        // timer started this build AND the combined report for this exact commit already exists.
        // Unset in every other case, including every failure of the precheck itself, so
        // "!= 'true'" is what makes an unset flag mean "run". These three stages are not special
        // in carrying it — so does every other stage in this file except 'Coverage precheck',
        // which is deliberately unguarded because it is the stage that SETS the flag. A skipped
        // nightly is therefore a near no-op rather than a full build with three stages missing.
        stage('Coverage (unit tests)') {
            when { expression { params.RUN_COVERAGE == true && env.NIGHTLY_COVERAGE_SKIP != 'true' } }
            steps {
                catchError(buildResult: 'UNSTABLE', stageResult: 'FAILURE') {
                    sh 'bash -c "source /opt/esp/idf/export.sh && make coverage"'
                    // Reached only when 'make coverage' exits 0; on failure catchError aborts
                    // the block and the flag stays unset — see 'Coverage (combined)' below
                    script { env.UNIT_COVERAGE_OK = 'true' }
                }
            }
            post {
                always {
                    // Also archive the per-test gcovr tracefiles under unittests/*/covr_report/ —
                    // they are the input 'make coverage-combined' merges, not just a rendered report
                    archiveArtifacts artifacts: "covr_report/**,unittests/**/covr_report/**", allowEmptyArchive: true
                }
            }
        }
        stage('Coverage (QEMU e2e)') {
            // Gated on RUN_COVERAGE ALONE. This stage used to also require RUN_E2E, on the
            // reasoning that 'make qemu-coverage' re-runs the very same e2e suite on an
            // instrumented build and that second, slower run must stay out of ordinary builds.
            // Keeping it out of ordinary builds is real, but it is already RUN_COVERAGE's OFF
            // default that does it: with RUN_COVERAGE=false no coverage stage runs at all, so
            // an ordinary build (RUN_COVERAGE=false, RUN_E2E=true) still runs the suite exactly
            // once. The extra RUN_E2E gate therefore bought nothing — and it cost the one
            // combination worth having, RUN_COVERAGE=true + RUN_E2E=false, because asking for
            // coverage forced the plain e2e stage to run too: ~2 h of e2e followed by ~2 h of
            // this stage re-running the same suite, ~4 h for a nightly that only wants numbers.
            // With the gate gone that nightly is a plain ~2 h coverage-only build, which is
            // what the parameterizedCron trigger at the top of this file asks for.
            // '== true' matches RUN_COVERAGE's OFF default: where the parameter is unregistered
            // and therefore null (a branch never built, or a newly added parameter — not merely
            // a changed default; see the e2e stage above), 'null == true' is false in Groovy, so
            // the stage is skipped. The e2e stage uses the mirror-image '!= false' because
            // RUN_E2E defaults ON. The two idioms differ on purpose — do not unify them.
            // Same port slot as the clean e2e stage above, for the same reason: this stage
            // re-runs the whole suite, so it brings up its own QEMU and needs its own host
            // port block. The two stages never overlap in time — they are sequential within one
            // build, and in a coverage-only run the clean stage does not run at all — so
            // sharing the executor's slot is correct, not a collision.
            environment { WB_MGE_PORT_SLOT = "${env.EXECUTOR_NUMBER ?: 0}" }
            when { expression { params.RUN_COVERAGE == true && env.NIGHTLY_COVERAGE_SKIP != 'true' } }
            steps {
                // Re-runs the e2e suite on an instrumented firmware build. 'make qemu-coverage'
                // (qemu.mk) passes pytest '--without-reboot', which deselects every test that
                // reboots the device — a reboot zeroes the in-RAM gcov counters before the
                // end-of-session /gcov dump. That is six whole FILES plus anything carrying
                // @pytest.mark.reboot, and api_tests/conftest.py (REBOOT_TEST_FILES together
                // with is_reboot()) is the single source of truth for which — read the list
                // there rather than trusting a paraphrase here, which would rot. Those paths
                // are absent from every coverage number this stage produces and from the
                // combined report below: what is measured is "coverage of the no-reboot
                // subset", not coverage of the whole suite.
                //
                // This stage deliberately publishes NO junit of its own. In a coverage-only
                // build (RUN_COVERAGE=true, RUN_E2E=false) the 'E2E tests (QEMU)' stage above
                // is skipped too, so nothing in the build publishes e2e junit at all. Exactly
                // what that does and does not cost:
                //   - NOT lost: whether the tests passed. pytest failing fails qemu-test-locked,
                //     api_tests/tree_lock.py propagates the child's exit code, so
                //     'make qemu-coverage' fails and the catchError below marks the build
                //     UNSTABLE. A SUCCESS build therefore DOES mean the no-reboot subset passed.
                //   - Lost: the per-test detail. Nothing reaches the Tests tab for e2e — no test
                //     names, durations, failure messages or history — leaving only the log and
                //     JUnit XML archived below, plus the console.
                //   - Never ran at all: the reboot files. A green build says nothing whatever
                //     about them, which is the claim actually worth guarding against.
                // (An earlier draft of this comment asserted that a green coverage-only build
                // says nothing about test outcomes. That was simply wrong — it came from the
                // change request rather than from the code — and is corrected here so that it
                // does not get copied onward.)
                // When RUN_E2E is on, the clean e2e stage above ran first and remains the
                // authoritative, per-test source for test results.
                //
                // Delete the clean run's outputs BEFORE running, so that presence becomes proof
                // of origin: after this, a file under these names can only have come from the
                // instrumented run below — which is exactly what the qemu_coverage_* copies in
                // post{always} claim about their contents. Without it, a coverage run that dies
                // before pytest ever starts (the COVERAGE=1 rebuild, flash-image packing, a
                // run_locked timeout) writes no new report, and post{always} would copy the CLEAN
                // run's green, all-tests-present report out under the coverage name — reboot
                // files included, which cannot exist in a coverage run at all. 'cp ... || true'
                // cannot tell "no file" from "someone else's file"; deleting up front can.
                // Nothing is lost: by the time this stage runs, the clean e2e stage has already
                // archived and published its own copies.
                sh 'rm -f build/qemu_test.log build/qemu_test_report.xml'
                catchError(buildResult: 'UNSTABLE', stageResult: 'FAILURE') {
                    sh 'bash -c "source /opt/esp/idf/export.sh && make qemu-coverage"'
                    // Reached only when 'make qemu-coverage' exits 0 — see 'Coverage (combined)'
                    script { env.QEMU_COVERAGE_OK = 'true' }
                }
            }
            post {
                always {
                    // Also archive the raw on-target coverage stream: 'make qemu-coverage-report'
                    // can rebuild the report from it offline, without re-running QEMU.
                    //
                    // The e2e log and JUnit XML are captured here too, as FILES only —
                    // deliberately not published as junit, see the steps comment above. This run
                    // produces them because 'make qemu-coverage' goes through the very same
                    // pytest session (qemu-test-locked writes the --junitxml, conftest writes
                    // build/qemu_test.log). With RUN_E2E=false the 'E2E tests (QEMU)' stage is
                    // skipped, so its post{always} never runs and nothing else would capture
                    // them; without this, the likeliest nightly failure — tests fail inside the
                    // instrumented run, the build goes UNSTABLE, coverage.stream ends up empty —
                    // would leave whoever investigates at 2 a.m. with console output and nothing
                    // else.
                    //
                    // They are COPIED to coverage-specific names first, and that rename is not
                    // cosmetic. The clean e2e stage archives the identical relative paths, and
                    // re-archiving a path within one build overwrites it. With RUN_COVERAGE and
                    // RUN_E2E both on — the one-off combination the parameters block describes —
                    // the Tests tab would then show the clean run's results while the downloadable
                    // XML silently held this run's, which has six fewer test files and no reboot
                    // coverage. Distinct names keep both runs' evidence, each labelled by which
                    // run produced it. '|| true' because the files are absent if the suite died
                    // before writing them, which is exactly when this post block must still run.
                    sh '''
                        cp build/qemu_test.log        build/qemu_coverage_test.log        2>/dev/null || true
                        cp build/qemu_test_report.xml build/qemu_coverage_test_report.xml 2>/dev/null || true
                    '''
                    archiveArtifacts artifacts: "build/qemu_coverage/**,build/coverage.stream,build/qemu_coverage_test.log,build/qemu_coverage_test_report.xml", allowEmptyArchive: true
                }
            }
        }
        stage('Coverage (combined)') {
            // Skipped unless BOTH producer stages completed successfully: 'make coverage-combined'
            // needs both inputs — the unit-test tracefiles and the QEMU one. A missing input would
            // hard-fail with a "run make ... first" error that masks the already reported real
            // cause, and a partially produced unit-test tracefile set would be worse still: the
            // merge would succeed and publish a silently incomplete report with understated
            // coverage. Success flags are used rather than file-existence checks precisely because
            // a partial unit-coverage run still leaves some tracefiles on disk. No RUN_E2E check
            // is needed here, but no longer for the reason once given: the QEMU coverage stage is
            // no longer gated on RUN_E2E and DOES run with e2e off, so "QEMU_COVERAGE_OK stays
            // unset" is not what protects this stage. What protects it is the success flag
            // itself — QEMU_COVERAGE_OK is set only after 'make qemu-coverage' exits 0, so if the
            // QEMU coverage stage is skipped OR fails, the flag is unset and this stage skips,
            // which is the whole point of checking flags rather than the parameter.
            // '== true' to match RUN_COVERAGE's OFF default, like the two producer stages. The
            // success flags already make this stage skip on their own, so the parameter check is
            // belt-and-braces here — but leaving it as '!= false' would read as if the default
            // were still ON and would be copied into the next gate that is not flag-guarded.
            // The NIGHTLY_COVERAGE_SKIP conjunct is redundant here — the producer stages carry it,
            // so on a skipped nightly their success flags are unset and this stage skips anyway —
            // but it is written out because every stage in this file carries the guard bar
            // 'Coverage precheck', which sets the flag, and a gap in that list reads as an
            // oversight rather than as a deduction.
            when { expression { params.RUN_COVERAGE == true && env.NIGHTLY_COVERAGE_SKIP != 'true' && env.UNIT_COVERAGE_OK == 'true' && env.QEMU_COVERAGE_OK == 'true' } }
            steps {
                // Merges the unit-test gcovr tracefiles with build/qemu_coverage/qemu_covr.json,
                // hence this stage runs after both coverage stages above
                catchError(buildResult: 'UNSTABLE', stageResult: 'FAILURE') {
                    sh 'bash -c "source /opt/esp/idf/export.sh && make coverage-combined"'
                    // Publish the merged figure so coverage gets what the four junit steps
                    // already give the tests: a number on the build page and a trend across
                    // builds, instead of an archive someone has to download. Inside the
                    // catchError and after the sh on purpose - coverage.xml exists only when
                    // the merge succeeded. gcovr is invoked with --xml, not --cobertura:
                    // the flag is spelled --cobertura only from gcovr 5.1, and the image
                    // installs whatever the distro ships, while --xml emits the same
                    // Cobertura document in every version from 3.x on.
                    recordCoverage(tools: [[parser: 'COBERTURA',
                                            pattern: 'build/combined_coverage/coverage.xml']])
                    // Record WHICH commit this combined report covers, for the 'Coverage
                    // precheck' stage at the top to read back. This is the single place that
                    // means "coverage was produced for this commit": the stage runs only when
                    // both producer stages succeeded, and this line is inside the catchError
                    // AFTER the merge, so it is reached only when the merge succeeded too. The
                    // combined report is also the whole point of a nightly — recording the
                    // commit in a producer stage, or in a post{always}, would claim coverage for
                    // a commit whose report was never produced, and the next nightly would skip.
                    script {
                        String coveredCommit = env.GIT_COMMIT
                        if (coveredCommit) {
                            try {
                                // Appended, never assigned over: the description may already say
                                // something. The precheck reads the LAST occurrence of the opening
                                // token for exactly that reason.
                                //
                                // The token is bracketed and punctuated — '[coverage-commit:<sha>]'
                                // — so that no prose can spell it by accident; the reader takes
                                // what sits between it and the next ']'. Keep it in sync with
                                // coverageMarkerOpen/coverageMarkerClose in 'Coverage precheck' at
                                // the top of this file: those two literals and this one are the
                                // whole contract between writer and reader.
                                String existing = currentBuild.description
                                String marker = "[coverage-commit:${coveredCommit}]"
                                currentBuild.description = existing ? "${existing} ${marker}" : marker
                            } catch (IOException e) {
                                // Not worth failing a good coverage build over. A missing marker
                                // only makes the next nightly measure this commit again.
                                //
                                // IOException is what setting a description declares — it writes
                                // the build record — and RuntimeException below covers the rest.
                                // Deliberately NOT 'Exception': that would also catch
                                // FlowInterruptedException, which extends InterruptedException,
                                // so a build cancelled during this write would have its
                                // cancellation rewritten into a "could not record" message and
                                // this script would carry on as though only the description had
                                // failed. Neither clause matches it, so the interruption leaves
                                // this script instead. It does not leave the STAGE: everything
                                // here sits inside the catchError above, whose catchInterruptions
                                // parameter defaults to true, so that wrapper absorbs the
                                // interruption and marks the stage FAILURE and the build UNSTABLE
                                // all the same. What the narrow clauses buy here is therefore
                                // only that a cancellation is not mislabelled as a failed
                                // description write. ('Coverage precheck' at the top of this file
                                // makes the same choice and gets more out of it, because that
                                // stage is wrapped in no catchError at all.)
                                echo "Could not record the covered commit in the build description (${e})"
                            } catch (RuntimeException e) {
                                // Same tolerance; see the type note on the clause above.
                                echo "Could not record the covered commit in the build description (${e})"
                            }
                        }
                    }
                }
            }
            post {
                always {
                    archiveArtifacts artifacts: "build/combined_coverage/**", allowEmptyArchive: true
                }
            }
        }
        stage('Upload to S3') {
            // Gated on one thing only: not a skipped nightly. Otherwise this fires on every build
            // of every branch, which is the intent — including a coverage-only nightly that
            // actually measures (RUN_COVERAGE=true, RUN_E2E=false) started by the
            // parameterizedCron trigger at the top of this file.
            //
            // The guard matters most HERE, and the reason is written into the paragraph below:
            // what an upload of an already-uploaded revision does could NOT be established from
            // this repository, and the standing advice is to assume a nightly on the default
            // branch DOES re-upload. An effect nobody can characterise, repeated 365 times a year
            // for revisions the daytime build already handed over, is not something to leave
            // running for want of a two-token 'when'. Uncertainty argues for not firing it, not
            // for firing it anyway.
            //
            // What that upload actually does: s3_uploader is a freestyle job whose only build
            // step clones wirenboard/wbfw-s3-uploader and runs s3-upload.sh, so the branch logic
            // lives in a separate repository. Its UPLOAD_FROM_BRANCH parameter ("Upload results to
            // S3 even if it is not master branch", default false, and this stage passes the
            // build's own value) reads as though non-release branches are skipped, but whether
            // 'main' counts as that release branch is exactly the part not verifiable here.
            // What it cannot do is publish instrumented firmware: 'Build firmware' copies
            // release/*.bin into result/ and archives them from there in its post { success }
            // block, both long before the coverage stages rebuild with instrumentation, and what
            // s3_uploader works from is this build's ARCHIVED artifacts.
            //
            // A second reason for the guard: a skipped nightly has no artifacts of its own. The
            // uploader is a separate job that copies the archived artifacts of the build it is
            // pointed at — hence copyArtifactPermission('/s3_uploader') in options at the top of
            // this file, and hence the UPSTREAM_JOB_NAME/BUILD parameters passed below. It runs
            // in its own workspace, possibly on another node, so this job's result/ directory is
            // not what it reads. On a skipped nightly 'Build firmware' never runs, so its
            // post { success } archiveArtifacts never runs either and the build ends with nothing
            // archived at all; the uploader pointed at that build number would find nothing to
            // copy and at best fail pointlessly. What it would NOT do is publish some earlier
            // build's binaries — the build it is given is this one.
            when { expression { env.NIGHTLY_COVERAGE_SKIP != 'true' } }
            steps {
                build job: 's3_uploader', parameters: [
                    string(name: 'UPSTREAM_JOB_NAME', value: env.JOB_NAME),
                    string(name: 'BUILD', value: env.BUILD_NUMBER),
                    booleanParam(name: 'UPLOAD_FROM_BRANCH', value: params.UPLOAD_FROM_BRANCH)
                ]
            }
        }
    }
}
