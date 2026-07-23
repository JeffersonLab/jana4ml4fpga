#include "apv_decode.hpp"

#include <algorithm>
#include <map>
#include <numeric>

#include "../calibration.hpp"
#include "../constants.hpp"

namespace gemreco {

ApvDecoded decode_apv_adc(const ApvRawBlock& block, const ApvPedestal& pedestal,
                          const AlgoParams& params) {
    ApvDecoded result;

    const size_t n_timebins = block.all_samples.size() / kChannelsCount;
    // Legacy guards (ApvDecodedDataFactory): skip malformed APVs
    if (n_timebins == 0 || block.all_samples.size() % kChannelsCount != 0
        || pedestal.offsets.size() != static_cast<size_t>(kChannelsCount)
        || pedestal.noises.size() != static_cast<size_t>(kChannelsCount)) {
        return result;
    }
    const auto& offsets = pedestal.offsets;

    result.common_mode_offsets.assign(n_timebins, 0.0);
    // LEGACY QUIRK 1 (preserved): "rawDataZS" is sized by the number of TIME BINS but
    // accumulates the first n_timebins CHANNEL values per time bin - almost certainly
    // a bug in the original (likely meant per-channel, size 128). Kept bit-for-bit.
    std::vector<double> raw_data_zs(n_timebins, 0.0);

    for (size_t t = 0; t < n_timebins; t++) {
        // Pedestal offset correction for this time bin
        std::vector<double> channel_values(kChannelsCount);
        for (int ch = 0; ch < kChannelsCount; ch++) {
            channel_values[ch] = block.all_samples[t * kChannelsCount + ch] - offsets[ch];
        }

        // LEGACY QUIRK 2 (preserved): value -> channel-index map; on duplicate values
        // the LAST channel index wins (std::map insert-or-assign in a forward loop).
        std::map<double, int> value_to_index;
        for (int ch = 0; ch < kChannelsCount; ch++) {
            value_to_index[channel_values[ch]] = ch;
        }

        // Common mode: take all channels, replace the N lowest by (-offset + baseline)
        std::vector<double> data_test = channel_values;
        std::sort(data_test.begin(), data_test.end());
        for (int i = 0; i < params.common_mode_cut; i++) {
            data_test[i] = -offsets[value_to_index[data_test[i]]] + params.apv_baseline;
        }
        const double common_mode =
            std::accumulate(data_test.begin(), data_test.end(), 0.0) / kChannelsCount;
        result.common_mode_offsets[t] = common_mode;

        // Common-mode correction (legacy applies it only to the ZS accumulation copy)
        for (auto& v : channel_values) v -= common_mode;
        for (size_t i = 0; i < raw_data_zs.size() && i < channel_values.size(); i++) {
            raw_data_zs[i] += channel_values[i];
        }
    }

    for (auto& v : raw_data_zs) v /= static_cast<double>(n_timebins);
    result.raw_data_average = std::move(raw_data_zs);

    // Decoded output: sign-flipped, pedestal- and common-mode-corrected raw values
    result.data.reserve(n_timebins);
    for (size_t t = 0; t < n_timebins; t++) {
        std::vector<double> row(kChannelsCount);
        for (int ch = 0; ch < kChannelsCount; ch++) {
            row[ch] = -(block.all_samples[t * kChannelsCount + ch] - offsets[ch]
                        - result.common_mode_offsets[t]);
        }
        result.data.push_back(std::move(row));
    }
    return result;
}

}  // namespace gemreco
