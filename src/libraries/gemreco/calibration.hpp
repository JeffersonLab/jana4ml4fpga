// gemreco: pedestal calibration with an explicit learn -> freeze -> apply lifecycle.
//
// Legacy behavior (PedestalFactory): a running per-channel mean/stddev evolves over
// the first `pedestal_events_to_average` events and every event is decoded against
// the *current* estimate. That is inherently sequential.
//
// This class supports both modes:
//  - replay/legacy mode: process_event() = add this event to the statistics AND
//    return the pedestal exactly as the legacy factory would have for this event
//    (bit-for-bit, used for golden-parity tests and single-threaded runs);
//  - MT mode: learn on a warm-up sample (process_event/add), then freeze() once and
//    share the immutable result across threads.
#pragma once

#include <map>
#include <mutex>
#include <vector>

#include <extmath/Average.h>
#include <extmath/StandardDeviation.h>

#include "config.hpp"
#include "constants.hpp"
#include "steps/decode_apv.hpp"

namespace gemreco {

struct ApvPedestal {
    std::vector<double> offsets;  ///< [kChannelsCount]
    std::vector<double> noises;   ///< [kChannelsCount]
};

/// Immutable after construction; share freely across threads.
struct Pedestals {
    std::map<int, ApvPedestal> apvs;
};

class PedestalBuilder {
public:
    explicit PedestalBuilder(const Config& cfg) : m_cfg(&cfg) {
        // Legacy PedestalFactory::Init: accumulators exist for every mapped APV
        for (const auto& [apv_id, info] : cfg.apvs) {
            m_averages[apv_id].resize(kChannelsCount);
            m_stddevs[apv_id].resize(kChannelsCount);
        }
    }

    /// Legacy-equivalent single call: learn from this event (while still in the
    /// averaging window) and return the pedestals the legacy factory would emit
    /// for THIS event (only APVs present in the event). Thread-safe (mutex).
    Pedestals process_event(const ApvGrouped& grouped) {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto& p = m_cfg->params;
        Pedestals result;

        for (const auto& [apv_id, block] : grouped) {
            if (!m_averages.count(apv_id)) continue;  // not in mapping

            const size_t n_timebins = block.all_samples.size() / kChannelsCount;

            // Accumulate (legacy: only while events_counter < events_to_average)
            if (m_events_counter < p.pedestal_events_to_average) {
                for (size_t t = 0; t < n_timebins; t++) {
                    for (int ch = 0; ch < kChannelsCount; ch++) {
                        double v = block.all_samples[t * kChannelsCount + ch];
                        m_averages[apv_id][ch].add(v);
                        m_stddevs[apv_id][ch].add(v);
                    }
                }
            }

            // Emit current estimate (legacy: constants before minimal_events)
            ApvPedestal ped;
            ped.offsets.reserve(kChannelsCount);
            ped.noises.reserve(kChannelsCount);
            for (int ch = 0; ch < kChannelsCount; ch++) {
                bool warmup = m_events_counter < p.pedestal_minimal_events;
                ped.noises.push_back(warmup ? p.pedestal_constant_noise
                                            : m_stddevs[apv_id][ch].stddev());
                ped.offsets.push_back(warmup ? p.pedestal_constant_offset
                                             : m_averages[apv_id][ch].mean());
            }
            result.apvs[apv_id] = std::move(ped);
        }

        m_events_counter++;  // legacy: incremented once per event, after all APVs
        return result;
    }

    /// Freeze the current statistics into an immutable Pedestals for the MT phase.
    /// APVs that never received data keep the warm-up constants.
    Pedestals freeze() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto& p = m_cfg->params;
        Pedestals result;
        for (const auto& [apv_id, avgs] : m_averages) {
            ApvPedestal ped;
            for (int ch = 0; ch < kChannelsCount; ch++) {
                bool no_stats = m_events_counter < p.pedestal_minimal_events;
                ped.noises.push_back(no_stats ? p.pedestal_constant_noise
                                              : m_stddevs.at(apv_id)[ch].stddev());
                ped.offsets.push_back(no_stats ? p.pedestal_constant_offset
                                               : avgs[ch].mean());
            }
            result.apvs[apv_id] = std::move(ped);
        }
        return result;
    }

    size_t events_seen() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_events_counter;
    }

private:
    const Config* m_cfg;
    std::map<int, std::vector<ml4fpga::extmath::Average>> m_averages;
    std::map<int, std::vector<ml4fpga::extmath::StandardDeviation>> m_stddevs;
    size_t m_events_counter = 0;
    mutable std::mutex m_mutex;
};

}  // namespace gemreco
