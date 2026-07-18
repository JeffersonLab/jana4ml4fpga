#include "decode_apv.hpp"

#include <algorithm>

#include "../constants.hpp"

namespace gemreco {

ApvGrouped group_by_apv(const RawEvent& event, const Config& cfg) {
    // Legacy: apvid_chan_sampls[apv][corrected_channel] = samples
    // (duplicate (apv, corrected channel) overwrites - keep that semantic)
    std::map<int, std::map<int, std::vector<int>>> grouped;
    for (const auto& hit : event.srs) {
        if (!cfg.has_apv(hit.apv_id)) continue;
        std::vector<int> samples(hit.samples.begin(), hit.samples.end());
        grouped[hit.apv_id][apv_channel_correction(hit.raw_channel_apv)] = std::move(samples);
    }

    ApvGrouped result;
    for (auto& [apv_id, channels] : grouped) {
        // Channels can be absent or have fewer samples in malformed events:
        // take the maximum sample count over channels (legacy behavior)
        size_t samples_size = 0;
        for (const auto& [ch, samples] : channels) {
            samples_size = std::max(samples_size, samples.size());
        }

        ApvRawBlock block;
        block.apv_id = apv_id;
        block.all_samples.assign(samples_size * kChannelsCount, 0.0);
        for (const auto& [ch, samples] : channels) {
            for (size_t sample_i = 0; sample_i < samples.size(); sample_i++) {
                size_t index = sample_i * kChannelsCount + static_cast<size_t>(ch);
                if (index < block.all_samples.size()) {
                    block.all_samples[index] = samples[sample_i];
                }
            }
        }
        result[apv_id] = std::move(block);
    }
    return result;
}

}  // namespace gemreco
