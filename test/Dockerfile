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
        net-tools iproute2 iputils-ping \
        unzip zip \
        htop lsof \
        jq \
        maven \
        mysql-client \
        python3-dev \
        libffi-dev \
        libxslt-dev \
        bash \
        tzdata \
        libssl-dev \
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
