FROM espressif/idf:release-v5.4

RUN curl -fsSL https://deb.nodesource.com/setup_20.x | bash -

RUN apt-get update && apt-get install -y \
    nodejs \
    build-essential \
    gcovr \
    && apt-get clean

# Mounted host repos have foreign UID — let git read .git for FIRMWARE_GIT_INFO
RUN git config --global --add safe.directory '*'

WORKDIR /root/esp/project

CMD ["/bin/bash"]
