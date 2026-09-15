#include <cstddef>
#include <cstdint>

#include "bench_common.hpp"

#include <slot_map.h>

#include "common.hpp"

using bench::Payload64;

namespace {
}

extern "C" void cleanroom_run_sergey(const bench::Sel* s) {
    // bench::run_adapter<SergeyAd>(*s);
}