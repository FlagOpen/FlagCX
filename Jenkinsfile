pipeline {
    agent any
    stages {
        stage('Build') {
            steps {
                echo 'Hello Jenkins...'
            }
        }
        stage('Check PR Comment') {
            steps {
                script {
                    def comment = env.CHANGE_COMMENT ?: ''
                    echo "PR comment: ${comment}"

                    if (!comment.contains("🚀") && !comment.contains(":rocket:")) {
                        echo "No rocket emoji found in comment, skipping e2e tests."
                        currentBuild.result = 'SUCCESS'
                        return
                    }
                }
            }
        }
        stage('Run Baseline E2E Tests') {
            steps {
                echo 'Running baseline e2e training...'
            }
        }
    }
}

