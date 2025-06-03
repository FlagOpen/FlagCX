properties([
  parameters([
    booleanParam(name: 'RUN_E2E', defaultValue: false, description: '是否运行E2E测试')
  ])
])

pipeline {
  agent {
    kubernetes {
      cloud 'bp-k8s-cluster'
      label 'vci-agent'
      defaultContainer 'main'
      yaml """
apiVersion: v1
kind: Pod
metadata:
  namespace: jenkins
spec:
  hostNetwork: true
  volumes:
    - name: dshm
      emptyDir:
        medium: Memory
        sizeLimit: "50G"
  containers:
    - name: jnlp
      image: harbor.local.clusters/bp/jenkins/inbound-agent:latest
      imagePullPolicy: IfNotPresent
      args:
        - -webSocket
        - \$(JENKINS_SECRET)
        - \$(JENKINS_NAME)
      env:
        - name: http_proxy
          value: http://10.1.2.1:7890
        - name: https_proxy
          value: http://10.1.2.1:7890
        - name: no_proxy
          value: localhost,127.0.0.1,jenkins,jenkins.jenkins.svc
    - name: main
      image: harbor.local.clusters/kubesphereio/infrawaves24.10@sha256:153c7ebd06473540fd6c740cb6b9acf1ec76d016904b8e66e09bee521609e923
      imagePullPolicy: IfNotPresent
      command: ["sleep", "inf"]
      env:
        - name: http_proxy
          value: http://10.1.2.1:7890
        - name: https_proxy
          value: http://10.1.2.1:7890
        - name: no_proxy
          value: localhost,127.0.0.1,jenkins,jenkins.jenkins.svc
      volumeMounts:
        - name: dshm
          mountPath: /dev/shm
      resources:
        limits:
          nvidia.com/gpu: '1'
      securityContext:
        capabilities:
          add: [IPC_LOCK, SYS_RESOURCE]
"""
    }
  }

  stages {
    stage('Verify') {
      steps {
        echo '✅ Running on Jenkins pod'
        sh 'env | grep -i proxy'
      }
    }

    stage('Unit Tests') {
      steps {
        sh '''
          echo "Running Unit tests..."
        '''
      }
    }

    stage('E2E test') {
      when {
        expression { return params.RUN_E2E }
      }
      steps {
        sh '''
          echo "Running E2E tests..."
        '''
      }
    }

    stage('Complete') {
      steps {
        echo '🎉 Pipeline completed successfully!'
      }
    }
  }
}

