#!/bin/bash
# Repeat the e2e suite N times, archiving each JUnit report for failure-set comparison.
mkdir -p /root/results
N=${1:-3}
TAG=${2:-full}
for i in $(seq 1 $N); do
  echo "=== $TAG run $i/$N start $(date -Is) ===" >> /root/series.log
  docker run --rm --name mge-series -v /root/wb-mge:/root/esp/project -w /root/esp/project \
    wb-mge-devenv bash -lc "source /opt/esp/idf/export.sh && make qemu-test" \
    >> /root/series.log 2>&1
  rc=$?
  cp /root/wb-mge/build/qemu_test_report.xml /root/results/${TAG}-${i}.xml 2>/dev/null
  echo "=== $TAG run $i/$N end rc=$rc $(date -Is) ===" >> /root/series.log
done
echo "=== SERIES_DONE $TAG $(date -Is) ===" >> /root/series.log
