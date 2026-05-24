FROM espressif/idf:release-v5.4

RUN curl -fsSL https://deb.nodesource.com/setup_20.x | bash -

RUN apt-get update && apt-get install -y \
    nodejs \
    build-essential \
    gcovr \
    && apt-get clean

# Mounted host repos have foreign UID — let git read .git for FIRMWARE_GIT_INFO
RUN git config --global --add safe.directory '*'

# Apply IDF patches at image build time so they are baked into the layer.
# Running during docker build avoids re-patching on every make invocation inside the container.
COPY patches/ /tmp/patches/
RUN python3 /tmp/patches/apply_idf_patch.py bug01-uart-driver-delete-intr-order.patch \
 && python3 /tmp/patches/apply_idf_patch.py bug04-openeth-isr-dram-log.patch \
 && python3 /tmp/patches/apply_idf_patch.py bug05-lact-timer-null-isr-guard.patch

WORKDIR /root/esp/project

CMD ["/bin/bash"]
