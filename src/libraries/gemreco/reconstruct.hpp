// gemreco: the one-call reconstruction entry point.
//
// MT mode (the target): calibration is FROZEN and shared immutably; reconstruct()
// is then a pure function of (event, config, calibration) + a per-thread Workspace,
// safe and deterministic from any number of threads.
//
// Legacy-sequential mode (parity/single-thread): drive the stages yourself with
// PedestalBuilder::process_event / SampleAnalyzer::process_event, exactly like the
// golden-parity tests do.
#pragma once

#include <string>
#include <vector>

#include "calibration.hpp"
#include "config.hpp"
#include "event_data.hpp"
#include "steps/cluster_match.hpp"
#include "steps/peak_find.hpp"
#include "steps/plane_merge.hpp"
#include "steps/rolling_baseline.hpp"
#include "workspace.hpp"

namespace gemreco {

/// Frozen (immutable) calibration shared across all threads.
struct FrozenCalibration {
    Pedestals pedestals;
    SampleBaselines sample_baselines;
};

struct RecoResult {
    PlanesDecoded planes;
    std::vector<PlanePeak> peaks;
    std::vector<Cluster> clusters;
    std::vector<Sample> samples;
};

/// Pure, thread-safe reconstruction of one event against frozen calibration.
/// plane_x/plane_y select the cluster-matching plane pair (legacy single-pair
/// limitation; the adapter can loop over pairs).
RecoResult reconstruct(const RawEvent& event, const Config& cfg,
                       const FrozenCalibration& calib, Workspace& ws,
                       const std::string& plane_x, const std::string& plane_y);

}  // namespace gemreco
