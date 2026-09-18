build-out := "./out/bench-release"
bench-binary-name := "slotmap-bench-clean"
full-bin-path := build-out / "benchmarks" / bench-binary-name

cmake-bench:
    cmake -S . -B {{build-out}} -DCMAKE_BUILD_TYPE=Release -DSLOTMAP_BUILD_BENCHMARKS=ON -G "Ninja"
    cmake --build {{build-out}}

bench-run:
    #!/usr/bin/env bash
    SCRIPT=$(realpath "{{full-bin-path}}")
    ./benchmarks/run_clean.sh $SCRIPT;

bench-run-single impl task size stride:
      MALLOC_TRIM_THRESHOLD_=-1 MALLOC_TOP_PAD_=268435456 taskset -c 2 \
        {{full-bin-path}} {{impl}} {{task}} {{size}} {{stride}}

profile impl task size stride perf_cmd:
    #!/usr/bin/env bash
    PIPE="/tmp/perf.ctl"

    if [[ ! -p "$PIPE" ]]; then
        mkfifo "$PIPE"
    fi

    source ./benchmarks/perfcfg.sh;

    CMD_NAME="{{perf_cmd}}"

    if [[ -z ${{perf_cmd}} ]]; then
        echo "No perf preset"
        exit 1
    fi

    declare -n TARGET_ARR="$CMD_NAME"

    sudo taskset -c 2 env MALLOC_TRIM_THRESHOLD_=-1 MALLOC_TOP_PAD_=268435456 PERF_CTL=$PIPE \
        "${TARGET_ARR[@]}" -- {{full-bin-path}} {{impl}} {{task}} {{size}} {{stride}}

    sudo chown -R $(whoami) perf.data




