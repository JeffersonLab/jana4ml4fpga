// W2-C determinism: after calibration freeze, reconstruct() must produce
// bit-for-bit identical results regardless of thread count or event order.
// This is the core guarantee of the immutable-config / frozen-calibration /
// per-thread-workspace model.
#include "catch_amalgamated.hpp"

#include <atomic>
#include <thread>

#include <gemreco/calibration.hpp>
#include <gemreco/config.hpp>
#include <gemreco/reconstruct.hpp>
#include <gemreco/steps/decode_apv.hpp>

#include "fixtures.hpp"

namespace {
const char* kPlaneX = "GEMTR1X";
const char* kPlaneY = "GEMTR1Y";

bool equal_results(const gemreco::RecoResult& a, const gemreco::RecoResult& b) {
    if (a.peaks.size() != b.peaks.size() || a.clusters.size() != b.clusters.size()
        || a.samples.size() != b.samples.size() || a.planes.size() != b.planes.size())
        return false;
    for (size_t i = 0; i < a.peaks.size(); i++) {
        if (a.peaks[i].index != b.peaks[i].index || a.peaks[i].height != b.peaks[i].height
            || a.peaks[i].area != b.peaks[i].area || a.peaks[i].plane_name != b.peaks[i].plane_name)
            return false;
    }
    for (size_t i = 0; i < a.clusters.size(); i++) {
        if (a.clusters[i].pos_x != b.clusters[i].pos_x
            || a.clusters[i].pos_y != b.clusters[i].pos_y
            || a.clusters[i].amplitude != b.clusters[i].amplitude)
            return false;
    }
    for (size_t i = 0; i < a.samples.size(); i++) {
        if (a.samples[i].id != b.samples[i].id || a.samples[i].value != b.samples[i].value
            || a.samples[i].rolling_average != b.samples[i].rolling_average
            || a.samples[i].is_noise != b.samples[i].is_noise)
            return false;
    }
    for (const auto& [plane, pd] : a.planes) {
        auto it = b.planes.find(plane);
        if (it == b.planes.end() || it->second.data != pd.data || it->second.noises != pd.noises)
            return false;
    }
    return true;
}
}  // namespace

TEST_CASE("reconstruct() is deterministic across threads and event order",
          "[determinism][mt]") {
    auto cfg = gemreco::load_config_csv(fixtures::dir());
    auto raw_events = fixtures::load_raw_events();
    REQUIRE(!raw_events.empty());

    // Learn calibration sequentially (legacy mode), then freeze
    gemreco::PedestalBuilder ped_builder(cfg);
    gemreco::SampleAnalyzer sample_analyzer(cfg);
    for (const auto& [evt, ev] : raw_events) {
        ped_builder.process_event(gemreco::group_by_apv(ev, cfg));
        sample_analyzer.process_event(ev);
    }
    gemreco::FrozenCalibration calib{ped_builder.freeze(), sample_analyzer.freeze()};

    // Reference: single-thread, in order
    std::vector<const gemreco::RawEvent*> events;
    for (const auto& [evt, ev] : raw_events) events.push_back(&ev);

    std::vector<gemreco::RecoResult> reference(events.size());
    {
        gemreco::Workspace ws;
        for (size_t i = 0; i < events.size(); i++) {
            reference[i] = gemreco::reconstruct(*events[i], cfg, calib, ws, kPlaneX, kPlaneY);
        }
    }

    SECTION("reverse order, single thread") {
        gemreco::Workspace ws;
        size_t mismatches = 0;
        for (size_t i = events.size(); i-- > 0;) {
            auto r = gemreco::reconstruct(*events[i], cfg, calib, ws, kPlaneX, kPlaneY);
            if (!equal_results(r, reference[i])) mismatches++;
        }
        REQUIRE(mismatches == 0);
    }

    SECTION("8 threads, interleaved") {
        const size_t n_threads = 8;
        std::vector<gemreco::RecoResult> mt_results(events.size());
        std::vector<std::thread> threads;
        for (size_t t = 0; t < n_threads; t++) {
            threads.emplace_back([&, t]() {
                gemreco::Workspace ws;  // per-thread workspace
                for (size_t i = t; i < events.size(); i += n_threads) {
                    mt_results[i] =
                        gemreco::reconstruct(*events[i], cfg, calib, ws, kPlaneX, kPlaneY);
                }
            });
        }
        for (auto& th : threads) th.join();

        size_t mismatches = 0;
        for (size_t i = 0; i < events.size(); i++) {
            if (!equal_results(mt_results[i], reference[i])) mismatches++;
        }
        REQUIRE(mismatches == 0);
        WARN("determinism: " << events.size() << " events identical across "
                             << n_threads << " threads");
    }
}
