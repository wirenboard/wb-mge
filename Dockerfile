FROM espressif/idf:release-v5.4

RUN curl -fsSL https://deb.nodesource.com/setup_20.x | bash -

RUN apt-get update && apt-get install -y \
    nodejs \
    build-essential \
    libc6-dev \
    libc6-dev-i386 \
    libc6-dev-x32 \
    gcovr \
    && apt-get clean

WORKDIR /root/esp/project

CMD ["/bin/bash"]
