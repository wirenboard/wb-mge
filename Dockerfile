FROM espressif/idf:release-v5.4

RUN apt-get update --no-cache && apt-get install -y --no-cache \
    nodejs \
    npm \
    build-essential \
    libc6-dev \
    libc6-dev-i386 \
    libc6-dev-x32 \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /root/esp/project

CMD ["/bin/bash"]
