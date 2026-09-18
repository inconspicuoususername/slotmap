#!/usr/bin/env bash
set -euo pipefail

# this exists because bench_external.cppm has the curse of Ra or something
# UPDATE: it's glibc's fault

SCRIPT=$1
echo "Using script $SCRIPT"

if [ ! -f "$SCRIPT" ]; then
    echo "Binary does not exist"
    exit
fi

for metric in insert find iterate churn churn_scan
do
  for impl in livealloc expandfn unrollfn sergey
  do
    for density in u32 p64
      do
        for stride in packed half
        do
           MALLOC_TRIM_THRESHOLD_=-1 MALLOC_TOP_PAD_=268435456 taskset -c 2 $SCRIPT $impl $metric $density $stride shutup;
           sleep 7;
        done
    done
  done
done