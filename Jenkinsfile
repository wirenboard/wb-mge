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
        // Serialise builds of this branch. This is about the QEMU e2e suite, not tidiness:
        // that suite is CPU-starvation sensitive, and under contention its data-path tests
        // get exactly zero bytes back (sent=..., got='' — never a partial read, and the
        // firmware does not crash). Two builds of the same branch running the e2e stage at
        // the same time on the same node ARE that contention, self-inflicted. Observed live:
        // builds #2 and #4 of this branch ran the QEMU stage simultaneously on
        // build-node-1-vm. Measured elsewhere: ~1.5x CPU oversubscription is already enough
        // to reproduce the cascade, while with no contention the same suite is green three
        // runs in a row. Removing this to drain the queue faster brings the flakiness back.
        //
        // Residual limitation, deliberately not solved here: this only stops the branch from
        // doubling up on ITSELF. The node is shared — wb-release-tests, wb-mqtt-zigbee,
        // release-test-orchestrator (x2) and wb-office-bot were all co-resident during the
        // runs above — and nothing in this file isolates the suite from them. The fixes for
        // that (a dedicated executor, a lock() from the Lockable Resources plugin, or cpuset
        // pinning) are owner-level CI decisions, and the installed plugin set could not be
        // confirmed from here.
        disableConcurrentBuilds();
    }
    parameters {
        booleanParam(name: 'UPLOAD_FROM_BRANCH', description: 'Upload results to S3 even if it is not master branch', defaultValue: false)
        // TEMPORARY on this branch: coverage is off by default so a build runs the e2e suite
        // once instead of twice — 'Coverage (QEMU e2e)' re-runs the whole suite on an
        // instrumented build. Flip back to true once the suite is settled and gating.
        booleanParam(name: 'RUN_COVERAGE', description: 'Run coverage (unit tests; the QEMU e2e and combined reports also need RUN_E2E). Temporarily off by default while the e2e suite is being stabilised', defaultValue: false)
        booleanParam(name: 'RUN_E2E', description: 'Run the QEMU e2e API suite; on by default. The QEMU coverage stage additionally needs RUN_COVERAGE', defaultValue: true)
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
            // on a node where it is not competing for CPU. Three clean runs is what we have so
            // far, which is not enough to tell a fixed suite from a lucky one. When that holds,
            // flip it by deleting the catchError wrapper below and leaving the post block as is.

            // Host-port isolation for the suite. Every host port it touches (QEMU hostfwd
            // targets, the two UART chardevs, the UDP IO bus) is derived from this one
            // integer by api_tests/qemu_ports.py, so two builds that land on the same node
            // no longer fight over 8080/50502-4/5561-2.
            //
            // EXECUTOR_NUMBER is Jenkins' per-executor index on the node, which is exactly
            // the granularity wanted: one concurrent build per executor, so distinct
            // concurrent builds get distinct blocks. This is CROSS-JOB isolation —
            // disableConcurrentBuilds() in options already serialises this branch against
            // ITSELF, and the residual exposure named in that comment is the other jobs
            // sharing the node (wb-release-tests, wb-mqtt-zigbee, ...). It does not fix CPU
            // contention, which is a separate and unsolved problem; it fixes the part where
            // two suites also collided on ports.
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
            // '!= false' is load-bearing: on the first build after a parameter default changes,
            // the parameters block has not taken effect yet and params.RUN_E2E is null. This
            // parameter defaults to ON, so null must mean "enabled"; in Groovy 'null != false'
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
        // (yellow) on coverage failure so the firmware upload still runs. Unit coverage needs
        // only RUN_COVERAGE; the QEMU one additionally needs RUN_E2E, because it re-runs the
        // e2e suite (see that stage). When it runs, it runs after the clean e2e stage.
        // '== true' is deliberate, and it flipped together with the default: the parameters
        // block only takes effect for the NEXT run, so on the first build after the default
        // changed params.RUN_COVERAGE is null. The default is now OFF, so that null has to
        // read as "disabled" — in Groovy 'null == true' is false, so the stage is skipped.
        // (It was '!= false' while the default was ON, for the mirror-image reason.)
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
            // Gated on RUN_E2E as well: 'make qemu-coverage' re-runs the very same e2e suite on
            // an instrumented build, so turning the gate off keeps that second, slower run out
            // of the build — the plain 'E2E tests (QEMU)' stage above already published junit.
            // Null-handling follows each parameter's own default: RUN_E2E defaults ON so null
            // means enabled ('!= false'), RUN_COVERAGE defaults OFF so null means disabled
            // ('== true'). The two idioms differ on purpose — do not unify them.
            // Same port slot as the clean e2e stage above, for the same reason: this stage
            // re-runs the whole suite, so it brings up its own QEMU and needs its own host
            // port block. The two stages never overlap in time (they are sequential within
            // one build), so sharing the executor's slot is correct, not a collision.
            environment { WB_MGE_PORT_SLOT = "${env.EXECUTOR_NUMBER ?: 0}" }
            when { expression { params.RUN_COVERAGE == true && params.RUN_E2E != false } }
            steps {
                // Re-runs the e2e suite on an instrumented firmware build with reboot/OTA tests
                // deselected (--without-reboot: a reboot zeroes the in-RAM gcov counters).
                // Both stages share the RUN_E2E gate, so whenever this stage runs the earlier
                // 'E2E tests (QEMU)' stage on the clean build ran too and stays authoritative
                // for test results — this stage deliberately publishes NO junit results.
                catchError(buildResult: 'UNSTABLE', stageResult: 'FAILURE') {
                    sh 'bash -c "source /opt/esp/idf/export.sh && make qemu-coverage"'
                    // Reached only when 'make qemu-coverage' exits 0 — see 'Coverage (combined)'
                    script { env.QEMU_COVERAGE_OK = 'true' }
                }
            }
            post {
                always {
                    // Also archive the raw on-target coverage stream: 'make qemu-coverage-report'
                    // can rebuild the report from it offline, without re-running QEMU
                    archiveArtifacts artifacts: "build/qemu_coverage/**,build/coverage.stream", allowEmptyArchive: true
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
            // is needed here: with e2e off the QEMU coverage stage never runs, so QEMU_COVERAGE_OK
            // stays unset and this stage skips on its own.
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
                }
            }
            post {
                always {
                    archiveArtifacts artifacts: "build/combined_coverage/**", allowEmptyArchive: true
                }
            }
        }
        stage('Upload to S3') {
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
