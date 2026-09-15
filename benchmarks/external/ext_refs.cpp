#include <cstddef>
#include <cstdint>
#include <vector>


#include "bench_common.hpp"

using bench::Payload64;
using bench::make_val;
using bench::run_lifecycle_elem;

namespace {
}

extern "C" void bench_external_sporacid() { run_lib<SporacidAd>(bench::N); }
extern "C" void bench_external_sergey() { run_lib<SergeyAd>(bench::N); }
// reduced live count: twiggler is capped at 65535 slots on GCC 16 (see TwigAd).
extern "C" void bench_external_twiggler() { run_lib<TwigAd>(1u << 15); }