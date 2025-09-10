pipeline {
    agent {
        dockerfile {
            reuseNode true
            label 'devenv'
            args '--entrypoint=""'
        }
    }
    options {
        copyArtifactPermission('/s3_uploader_test');
    }
    parameters {
        booleanParam(name: 'UPLOAD_FROM_BRANCH', description: 'Upload results to S3 even if it is not master branch', defaultValue: false)
    }

    stages {
        stage('Cleanup') {
            steps {
                script {
                    sh 'bash -c "source /opt/esp/idf/export.sh && make clean"'
                }
            }
        }
        stage('Build') {
            steps {
                script {
                    sh 'bash -c "source /opt/esp/idf/export.sh && make"'
                }
            }
            post {
                success {
                    archiveArtifacts artifacts: "release/*.bin"
                }
            }
        }
        stage('S3 Upload') {
            steps {
                build job: 's3_uploader_test', parameters: [
                    string(name: 'UPSTREAM_JOB_NAME', value: env.JOB_NAME),
                    string(name: 'BUILD', value: env.BUILD_NUMBER),
                    string(name: 'PROJECT', value: 'WB-MGE'),
                    booleanParam(name: 'UPLOAD_FROM_BRANCH', value: params.UPLOAD_FROM_BRANCH)
                ]
            }
        }
    }
}
