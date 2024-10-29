pipeline {
    agent {
        dockerfile {
            reuseNode true
            label 'devenv'
            args '--entrypoint=""'
        }
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
                    archiveArtifacts artifacts: "build/*.bin"
                }
            }
        }
    }
}
