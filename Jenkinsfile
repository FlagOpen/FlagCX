pipeline {
    agent any
    options {
        timeout(time: 60, unit: 'MINUTES')    
        buildDiscarder(logRotator(numToKeepStr: '10'))  
    }
    stages {
        stage('Build') {
            steps {
                echo 'Hello Jenkins...'
            }
        }
        stage('Unit Test & Coverage') {
            steps {
                sh '''
                cd build
                lcov --capture --directory . --output-file coverage.info
                lcov --remove coverage.info '/usr/*' --output-file coverage.info 
                genhtml coverage.info --output-directory coverage-report
                '''
            }
            post {
                always {
                    junit 'build/Testing/**/*.xml'
                    cobertura coberturaReportFile: 'build/coverage-report/coverage.xml'
                }
            }
        }
        stage('Coverage Check') {
            steps {
                script {
                    def coverage = currentBuild.rawBuild.getAction(hudson.plugins.cobertura.CoberturaBuildAction)
                        ?.getCoverage(hudson.plugins.cobertura.targets.CoverageMetric.LINE)
                        ?.getPercentage() ?: 0
                    echo "Code coverage: ${coverage}%"
                    if (coverage < 90) {
                        error "Code coverage below 90%, failing the build."
                    }
                }
            }
        }
    }
    post {
        success {
            echo 'Pipeline completed successfully!'
        }
        failure {
            echo 'Pipeline failed!'
        }
    }
}

