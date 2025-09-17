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
        stage('Cleanup') {
            steps {
                script {
                    sh 'bash -c "source /opt/esp/idf/export.sh && make clean"'
                    sh 'rm -rf result/'
                }
            }
        }
        stage('Build') {
            steps {
                script {
                    sh 'bash -c "source /opt/esp/idf/export.sh && make"'
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
        stage('S3 Upload') {
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
