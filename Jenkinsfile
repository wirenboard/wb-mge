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
    }
    parameters {
        booleanParam(name: 'UPLOAD_FROM_BRANCH', description: 'Upload results to S3 even if it is not master branch', defaultValue: false)
        booleanParam(name: 'RUN_COVERAGE', description: 'Run coverage (unit + QEMU e2e + combined report)', defaultValue: true)
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
                // catchError keeps build UNSTABLE (yellow) on lint findings — QEMU/S3 still run
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
            steps {
                // catchError keeps build UNSTABLE (yellow) on e2e failure — S3 Upload still runs
                catchError(buildResult: 'UNSTABLE', stageResult: 'FAILURE') {
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
        // Coverage stages run after the clean E2E run and before S3 upload; catchError keeps
        // the build UNSTABLE (yellow) on coverage failure so the firmware upload still runs.
        // '!= false' is deliberate: the parameters block only takes effect for the NEXT run,
        // so on the first build after RUN_COVERAGE was introduced params.RUN_COVERAGE is null
        // — treat that null as "enabled" instead of silently skipping all coverage stages
        stage('Coverage (unit tests)') {
            when { expression { params.RUN_COVERAGE != false } }
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
            when { expression { params.RUN_COVERAGE != false } }
            steps {
                // Re-runs the e2e suite on an instrumented firmware build with reboot/OTA tests
                // deselected (--without-reboot: a reboot zeroes the in-RAM gcov counters).
                // The earlier 'E2E tests (QEMU)' stage on the clean build stays authoritative
                // for test results, so this stage deliberately publishes NO junit results.
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
            // a partial unit-coverage run still leaves some tracefiles on disk.
            when { expression { params.RUN_COVERAGE != false && env.UNIT_COVERAGE_OK == 'true' && env.QEMU_COVERAGE_OK == 'true' } }
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
