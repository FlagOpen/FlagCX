FROM ubuntu:22.04

# 设置环境变量
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

# 安装常用工具和开发工具链
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        python3 python3-pip \
        nodejs npm \
        curl wget git vim \
        build-essential \
        ca-certificates \
        net-tools iproute2 iputils-ping \
        unzip zip \
        htop lsof \
        jq \
        openjdk-11-jdk \
        maven \
        mysql-client \
        redis-tools \
        python3-dev \
        libffi-dev \
        libxml2-dev \
        libxslt-dev \
        bash \
        tzdata \
        libssl-dev \
        zlib1g-dev \
        libpq-dev \
        python3-pandas \
        python3-numpy \
        python3-scipy \
        python3-sklearn && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# 验证版本
RUN python3 --version && node -v && npm -v && gcc --version && \
    java -version && \
    mvn -v

# 设置工作目录
WORKDIR /app

# 拷贝脚本或工具（可选）
# COPY ./your-script.py ./run.py

# 暴露端口（假设运行某些服务）
EXPOSE 5000

CMD [ "bash" ]
