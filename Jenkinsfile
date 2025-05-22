pipeline {
    agent any
    options {
        timeout(time: 60, unit: 'MINUTES')    
        buildDiscarder(logRotator(numToKeepStr: '10'))  
    }
    stages {
        stage('Build') {
            steps {
                echo 'Build started...'
                sh '''
                mkdir -p build
                cd build
                '''
            }
        }
        stage('Code Style Check') {
   	    steps {
        	script {
            	    def files = sh(script: "git ls-files '*.cpp' '*.h'", returnStdout: true).trim().split('\n')
            	    for (file in files) {
                	def result = sh(script: "clang-format -output-replacements-xml ${file} | grep -q '<replacement>'", returnStatus: true)
                	if (result == 0) {
                    	   error "Code format mismatch: ${file}"
               		}
            	    }
        	}
        	sh '''
        	pip install cpplint
        	cpplint $(git ls-files '*.cpp' '*.h')
        	cppcheck --enable=all --inconclusive --error-exitcode=1 .
        	'''
    	    }
	}

        stage('Unit Test & Coverage') {
            steps {
                sh '''
                cd build
                ctest --output-on-failure
                # 生成覆盖率信息，需编译时加 -fprofile-arcs -ftest-coverage
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

