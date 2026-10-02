build-out-rel := "./out/Release"
bench-binary-name := "slotmap-bench-clean"
full-bin-path := build-out-bench / "benchmarks" / bench-binary-name

cmake-bench:
    cmake -S . -B {{build-out-bench}} -DCMAKE_BUILD_TYPE=Release -DSLOTMAP_BUILD_BENCHMARKS=ON -G "Ninja"
    cmake --build {{build-out-bench}}

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

build-out-dbg := "./out/Debug"
build-out-bench := "./out/bench"

test type:
    #!/usr/bin/env bash

    TYPE={{type}}
    DIR=""

    if [[ "$TYPE" == "Release" ]]; then
        DIR="{{build-out-rel}}"
    elif [ "$TYPE" == "Debug" ]; then
        DIR="{{build-out-dbg}}"
    else
        echo "Tests can be for Release or Debug.";
        exit 1;
    fi

    cmake -S . -B $DIR -DSLOTMAP_BUILD_DEMO=OFF -DSLOTMAP_BUILD_TESTS=ON -G Ninja -DCMAKE_BUILD_TYPE="$TYPE"
    cmake --build "$DIR"

    ctest --test-dir "$DIR" --output-on-failure