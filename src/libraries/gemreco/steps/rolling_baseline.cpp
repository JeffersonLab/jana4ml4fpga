#include "rolling_baseline.hpp"

#include <cmath>

#include "../constants.hpp"

namespace gemreco {

std::vector<Sample> SampleAnalyzer::process_event(const RawEvent& event) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<Sample> result;
    if (event.srs.empty()) return result;

    // LEGACY BUG (preserved for bit-parity): the legacy code checks
    // GetAPVOrientation(result->apv) BEFORE assigning result->apv, on a
    // value-initialized object - i.e. it always reads APV id 0's orientation
    // (0 if APV 0 is unmapped, via std::map::operator[] default-insert).
    // The channel flip therefore applies to ALL samples iff APV 0 has
    // orientation != 0, and never depends on the sample's own APV.
    const int orientation_of_apv0 =
        m_cfg->apvs.count(0) ? m_cfg->apvs.at(0).orientation : 0;

    std::map<uint64_t, Sample> sorted;  // legacy: sorted by id, duplicates overwrite

    for (const auto& hit : event.srs) {
        auto apv_it = m_cfg->apvs.find(hit.apv_id);
        if (apv_it == m_cfg->apvs.end()) continue;
        const ApvInfo& apv = apv_it->second;

        for (size_t sample_index = 0; sample_index < hit.samples.size(); sample_index++) {
            Sample s;
            s.raw_channel = static_cast<uint32_t>(hit.raw_channel_apv);
            s.channel = static_cast<uint32_t>(apv_channel_correction(hit.raw_channel_apv));
            if (orientation_of_apv0) {
                s.channel = kChannelsCount - 1 - s.channel;
            }
            s.apv = static_cast<uint32_t>(hit.apv_id);
            s.time_bin = static_cast<uint32_t>(sample_index);
            // Legacy uses GetPlaneID (axis 0/1) and the never-populated
            // GetDetectorFromAPVIDMap -> detector id 0 (see ApvInfo comment)
            s.plane = static_cast<uint32_t>(apv.sample_plane_id);
            s.detector = static_cast<uint32_t>(apv.sample_detector_id);
            const uint64_t apv_index_on_plane = static_cast<uint64_t>(apv.index_on_plane);

            s.id = s.channel
                   + s.apv * 1000ull
                   + apv_index_on_plane * 1000000ull
                   + s.time_bin * 1000000000ull
                   + s.plane * 1000000000000ull
                   + s.detector * 1000000000000000ull
                   + 1000000000000000000ull;

            s.raw_value = hit.samples[sample_index];

            // Rolling statistics: add first, then read (legacy order)
            auto stat_it = m_stats.find(s.id);
            if (stat_it == m_stats.end()) {
                stat_it = m_stats.emplace(s.id, ml4fpga::extmath::RollingStatistics<double>(
                                                    m_params.stat_len)).first;
            }
            stat_it->second.add(s.raw_value);
            s.rolling_average = stat_it->second.getAverage();
            s.rolling_std = stat_it->second.getStandardDeviation();

            s.value = -(s.raw_value - s.rolling_average);

            if (m_params.filter_by_sigma) {
                s.is_noise = std::abs(s.raw_value - s.rolling_average)
                             < m_params.filter_sigmas * s.rolling_std;
            } else {
                s.is_noise = std::abs(s.raw_value - s.rolling_average)
                             < m_params.filter_threshold;
            }

            sorted[s.id] = s;
        }
    }

    result.reserve(sorted.size());
    for (const auto& [id, s] : sorted) result.push_back(s);
    return result;
}

SampleBaselines SampleAnalyzer::freeze() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    SampleBaselines out;
    for (const auto& [id, stat] : m_stats) {
        out.avg_std[id] = {stat.getAverage(), stat.getStandardDeviation()};
    }
    return out;
}

std::vector<Sample> analyze_samples_frozen(const RawEvent& event, const Config& cfg,
                                           const SampleBaselines& baselines,
                                           const SampleParams& params) {
    std::vector<Sample> result;
    if (event.srs.empty()) return result;

    const int orientation_of_apv0 = cfg.apvs.count(0) ? cfg.apvs.at(0).orientation : 0;
    std::map<uint64_t, Sample> sorted;

    for (const auto& hit : event.srs) {
        auto apv_it = cfg.apvs.find(hit.apv_id);
        if (apv_it == cfg.apvs.end()) continue;
        const ApvInfo& apv = apv_it->second;

        for (size_t sample_index = 0; sample_index < hit.samples.size(); sample_index++) {
            Sample s;
            s.raw_channel = static_cast<uint32_t>(hit.raw_channel_apv);
            s.channel = static_cast<uint32_t>(apv_channel_correction(hit.raw_channel_apv));
            if (orientation_of_apv0) s.channel = kChannelsCount - 1 - s.channel;
            s.apv = static_cast<uint32_t>(hit.apv_id);
            s.time_bin = static_cast<uint32_t>(sample_index);
            s.plane = static_cast<uint32_t>(apv.sample_plane_id);
            s.detector = static_cast<uint32_t>(apv.sample_detector_id);
            const uint64_t apv_index_on_plane = static_cast<uint64_t>(apv.index_on_plane);
            s.id = s.channel + s.apv * 1000ull + apv_index_on_plane * 1000000ull
                   + s.time_bin * 1000000000ull + s.plane * 1000000000000ull
                   + s.detector * 1000000000000000ull + 1000000000000000000ull;

            s.raw_value = hit.samples[sample_index];

            auto b = baselines.avg_std.find(s.id);
            if (b != baselines.avg_std.end()) {
                s.rolling_average = b->second.first;
                s.rolling_std = b->second.second;
            } else {
                s.rolling_average = s.raw_value;  // unseen id: neutral baseline
                s.rolling_std = 0;
            }

            s.value = -(s.raw_value - s.rolling_average);
            if (params.filter_by_sigma) {
                s.is_noise = std::abs(s.raw_value - s.rolling_average)
                             < params.filter_sigmas * s.rolling_std;
            } else {
                s.is_noise = std::abs(s.raw_value - s.rolling_average)
                             < params.filter_threshold;
            }
            sorted[s.id] = s;
        }
    }

    result.reserve(sorted.size());
    for (const auto& [id, s] : sorted) result.push_back(s);
    return result;
}

}  // namespace gemreco
