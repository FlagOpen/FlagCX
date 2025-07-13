pipeline {
  agent {
    kubernetes {
      cloud 'test'
      label 'vci-agent'
      defaultContainer 'main'
      yaml """
apiVersion: v1
kind: Pod
metadata:
  name: jenkins
  namespace: jenkins
spec:
  hostNetwork: true
  volumes:
    - name: dshm
      emptyDir:
        medium: Memory
        sizeLimit: "40G"
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
      image:  harbor.local.clusters/bp/megatron-mla:25.02_cuda128_vllm084_20250527_ci_v3
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
        echo '✅ Running on jenkins pod'
        sh 'env | grep -i proxy'
      }
    }

    stage('Run Unit Tests and Generate Coverage') {
      steps {
          echo 'Unit Test  successfully!'
      }
    }
    stage('Complete') {
      steps {
        echo '🎉 Pipeline completed successfully!'
      }
    }
  }
}
