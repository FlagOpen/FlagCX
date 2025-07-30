FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        python3 python3-pip \
        nodejs npm \
        curl wget git vim \
        build-essential \
        ca-certificates \
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


RUN python3 --version && node -v && npm -v && gcc --version && \
    java -version && \
    mvn -v

WORKDIR /app

EXPOSE 5000

CMD [ "bash" ]
