// gemreco: per-thread scratch state. One Workspace per thread, never shared.
// Reused across events to avoid reallocation (reset() clears, keeps capacity where
// containers allow). Buffer-pool optimization comes with the benchmarking phase.
#pragma once

#include <map>

#include "steps/apv_decode.hpp"

namespace gemreco {

struct Workspace {
    std::map<int, ApvDecoded> decoded;

    void reset() { decoded.clear(); }
};

}  // namespace gemreco
