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
        }
        stage('Lint comments') {
            steps {
                catchError(buildResult: 'UNSTABLE', stageResult: 'FAILURE') {
                    sh 'make lint-comments'
                }
            }
        }
        stage('Unit tests (frontend + C)') {
            steps {
                script {
                    sh 'bash -c "source /opt/esp/idf/export.sh && make unittests test-frontend"'
                }
            }
        }
        stage('Build firmware') {
            steps {
                script {
                    sh 'bash -c "source /opt/esp/idf/export.sh && make build-frontend build-idf-project"'
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
                    // Archive QEMU log for debugging regardless of test outcome
                    archiveArtifacts artifacts: "build/qemu_test.log", allowEmptyArchive: true
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
