// gemreco stage 7: per-sample rolling-baseline analysis.
// Faithful port of the legacy SampleAnalysisFactory (plugins/gemrecon).
//
// Stateful across events (rolling window per sample id) - calibration tier:
// mutable behind a mutex while learning; freeze semantics come with the MT
// adapter (frozen snapshot of the rolling averages).
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

#include <extmath/RollingStatistics.h>

#include "../config.hpp"
#include "../event_data.hpp"

namespace gemreco {

struct Sample {
    uint64_t id = 0;
    uint32_t channel = 0;
    uint32_t raw_channel = 0;
    uint32_t time_bin = 0;
    uint32_t apv = 0;
    uint32_t plane = 0;
    uint32_t detector = 0;
    bool is_noise = false;
    double value = 0;
    double raw_value = 0;
    double rolling_average = 0;
    double rolling_std = 0;
};

struct SampleParams {
    size_t stat_len = 500;        ///< rolling window length (legacy "sample:stat_len")
    bool filter_by_sigma = true;  ///< legacy filter_algo == "sigma"
    double filter_sigmas = 3;
    double filter_threshold = 3;
};

/// Frozen per-sample-id baselines (immutable snapshot of the rolling statistics).
struct SampleBaselines {
    std::map<uint64_t, std::pair<double, double>> avg_std;  ///< id -> (average, stddev)
};

class SampleAnalyzer {
public:
    SampleAnalyzer(const Config& cfg, SampleParams params = {})
        : m_cfg(&cfg), m_params(params) {}

    /// Analyze one event; updates the rolling statistics (thread-safe via mutex).
    /// Output is sorted by sample id; duplicate ids within an event overwrite
    /// (legacy std::map semantics).
    std::vector<Sample> process_event(const RawEvent& event);

    /// Immutable snapshot of the current rolling statistics for the MT phase.
    SampleBaselines freeze() const;

private:
    const Config* m_cfg;
    SampleParams m_params;
    std::map<uint64_t, ml4fpga::extmath::RollingStatistics<double>> m_stats;
    mutable std::mutex m_mutex;
};

/// Frozen-baseline sample analysis: identical math to SampleAnalyzer::process_event
/// but read-only against the snapshot - pure and thread-safe. Ids never seen during
/// the learn phase get (average = raw_value, stddev = 0) -> value 0, is_noise false.
std::vector<Sample> analyze_samples_frozen(const RawEvent& event, const Config& cfg,
                                           const SampleBaselines& baselines,
                                           const SampleParams& params = {});

}  // namespace gemreco
