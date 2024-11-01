FROM espressif/idf:release-v5.4

RUN apt-get update && apt-get install -y \
    nodejs \
    npm \
    build-essential \
    libc6-dev \
    libc6-dev-i386 \
    libc6-dev-x32 \
    && apt-get clean

WORKDIR /root/esp/project

CMD ["/bin/bash"]
