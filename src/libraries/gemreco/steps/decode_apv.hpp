// gemreco stage 1: group raw SRS channels by APV into the canonical sample layout.
// Faithful port of the legacy RawDataFactory::Process (plugins/gemrecon).
#pragma once

#include <map>
#include <vector>

#include "../config.hpp"
#include "../event_data.hpp"

namespace gemreco {

/// One APV's samples in the canonical layout:
/// [128 strip values at time 0, 128 strip values at time 1, ...]
/// (index = time_i * kChannelsCount + corrected_channel)
struct ApvRawBlock {
    int apv_id = -1;
    std::vector<double> all_samples;
};

using ApvGrouped = std::map<int, ApvRawBlock>;

/// Groups SRS channel windows by APV id, applies APV25 channel de-multiplexing
/// (apv_channel_correction) and lays samples out time-major. APVs not present in
/// the config mapping are dropped. Missing channels stay 0.
ApvGrouped group_by_apv(const RawEvent& event, const Config& cfg);

}  // namespace gemreco
