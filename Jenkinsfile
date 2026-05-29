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
