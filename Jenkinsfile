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
    parameters {
        booleanParam(name: 'UPLOAD_FROM_BRANCH', description: 'Upload results to S3 even if it is not master branch', defaultValue: false)
        // Coverage is off by default on purpose, and permanently: an ordinary build is meant to
        // run the e2e suite ONCE, and 'Coverage (QEMU e2e)' re-runs the whole suite on an
        // instrumented build, so switching this on doubles a ~2 h build. This default is the
        // policy, not a stopgap.
        //
        // What produces coverage instead is ci/nightly-coverage.Jenkinsfile, which triggers a
        // build with RUN_COVERAGE=true, RUN_E2E=false. Do not read that as "this branch gets
        // nightly coverage" — its reach is narrower than it sounds, in three separate ways:
        //   * 'main' only. Its stage is gated by
        //     `when { expression { !env.BRANCH_NAME || env.BRANCH_NAME in ['main'] } }`, so no
        //     feature branch ever gets a nightly coverage build unless it is added to that
        //     list by hand. This Jenkinsfile, by contrast, is read on every branch.
        //   * At most once a day, and only when there are new commits. The trigger is
        //     pollSCM('H 2 * * *'), not cron: a build happens only if the branch head moved
        //     since that job's last poll baseline, so a day without pushes produces no
        //     nightly at all.
        //   * Only if that job has actually been created. It lives in a SECOND multibranch
        //     project that must be configured by hand over this repository (see the DEPLOYMENT
        //     block in that file, which also warns that a misconfiguration fails silently).
        //     Nothing in this repository proves it exists.
        // So outside 'main' — and on 'main' too until that job is confirmed deployed — the
        // only coverage that runs is coverage somebody asks for by setting this parameter.
        //
        // The two parameters are INDEPENDENT gates, which is exactly what makes a coverage-only
        // build possible:
        //   RUN_COVERAGE=false, RUN_E2E=true  — ordinary build: the e2e suite runs once, ~2 h;
        //   RUN_COVERAGE=true,  RUN_E2E=false — nightly coverage build: unit coverage + QEMU
        //                                       coverage + combined report, ~2 h, no separate
        //                                       plain e2e run. Fired by ci/nightly-coverage.Jenkinsfile.
        // Setting both on runs the suite twice (~4 h) and is only worth it for a one-off.
        // These describe only which of the GATED stages run. Everything ungated runs either way —
        // lints, unit tests, frontend, firmware build, and the unconditional 'Upload to S3',
        // which means even a coverage-only nightly re-invokes s3_uploader for the same revision
        // the day's push build already handed to it. See that stage for what is and is not known
        // about the consequences.
        booleanParam(name: 'RUN_COVERAGE', description: 'Run the coverage stages: unit tests and QEMU e2e. The combined report additionally requires both of those to succeed. Independent of RUN_E2E. Off by default: coverage runs when it is explicitly requested here. The separate nightly pipeline covers main only, at most once a day and only when there are new commits — and only if that job exists, which nothing in this repository proves', defaultValue: false)
        booleanParam(name: 'RUN_E2E', description: 'Run the plain QEMU e2e API suite and publish its junit. On by default, and meant to stay on: the suite is expected to run on every build. Does not gate the coverage stages — with RUN_COVERAGE on, the QEMU coverage stage re-runs the suite regardless', defaultValue: true)
    }

    stages {
        stage('Cleanup workspace') {
            steps {
                script {
                    sh 'bash -c "source /opt/esp/idf/export.sh && make clean"'
                    sh 'rm -rf result/'
                }
            }
        }
        stage('Lint frontend (ESLint)') {
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
            steps {
                catchError(buildResult: 'UNSTABLE', stageResult: 'FAILURE') {
                    sh 'make lint-comments'
                }
            }
        }
        stage('Unit tests (C)') {
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
            steps {
                script {
                    sh 'make build-frontend'
                }
            }
        }
        stage('Build firmware') {
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
            when { expression { params.RUN_E2E != false } }
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
        stage('Coverage (unit tests)') {
            when { expression { params.RUN_COVERAGE == true } }
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
            // what ci/nightly-coverage.Jenkinsfile asks for.
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
            when { expression { params.RUN_COVERAGE == true } }
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
            when { expression { params.RUN_COVERAGE == true && env.UNIT_COVERAGE_OK == 'true' && env.QEMU_COVERAGE_OK == 'true' } }
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
                }
            }
            post {
                always {
                    archiveArtifacts artifacts: "build/combined_coverage/**", allowEmptyArchive: true
                }
            }
        }
        stage('Upload to S3') {
            // UNCONDITIONAL — no 'when' here, so this fires on every build of every branch,
            // including a coverage-only nightly (RUN_COVERAGE=true, RUN_E2E=false) triggered by
            // ci/nightly-coverage.Jenkinsfile. That nightly therefore re-invokes s3_uploader for
            // the same revision the day's ordinary push build already handed to it — the action
            // is certain, the outcome is not. What that upload actually does could NOT be
            // established from this repo: s3_uploader is a freestyle job whose
            // only build step clones wirenboard/wbfw-s3-uploader and runs s3-upload.sh, so the
            // branch logic lives in a separate repository. Its UPLOAD_FROM_BRANCH parameter
            // ("Upload results to S3 even if it is not master branch", default false, and this
            // stage passes the build's own value) reads as though non-release branches are
            // skipped, but whether 'main' counts as that release branch is exactly the part not
            // verifiable here. Treat a nightly on the default branch as though it DOES re-upload.
            // What it cannot do is publish instrumented firmware: 'Build firmware' copies
            // release/*.bin into result/ long before the coverage stages rebuild with
            // instrumentation, and s3_uploader copies artifacts from result/.
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
