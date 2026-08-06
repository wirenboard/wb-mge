#!/bin/bash
# Reproduce CI-like scheduling jitter: run N CPU hogs alongside the e2e test.
# Hogs carry the WBHOG marker and are killed on any exit path.
HOGS=${1:-24}
shift
ARGS="$*"
cleanup() { pkill -9 -f WBHOG 2>/dev/null; }
trap cleanup EXIT INT TERM HUP
for i in $(seq 1 $HOGS); do sh -c "while :; do :; done # WBHOG" & done
sleep 1
echo "hogs: $(pgrep -f WBHOG | wc -l), cores: $(nproc)"
docker run --rm --name mge-load -v /root/wb-mge:/root/esp/project -w /root/esp/project \
  wb-mge-devenv bash -lc "source /opt/esp/idf/export.sh && make qemu-test PYTEST_ARGS=\"$ARGS\""
rc=$?
cleanup; sleep 1
echo "hogs left: $(pgrep -f WBHOG | wc -l)"
exit $rc
