// W2-D MT scaling benchmark for gemreco::reconstruct().
// Loads the golden fixtures, freezes calibration, then measures throughput with
// 1..N threads sharing the immutable Config/Calibration, each with its own
// Workspace. Prints an events/s table (validate scaling from the numbers).
//
// Usage: GEMRECO_FIXTURES=<dir> ./gemreco_bench [max_threads] [repeats]
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#include <gemreco/calibration.hpp>
#include <gemreco/config.hpp>
#include <gemreco/reconstruct.hpp>
#include <gemreco/steps/decode_apv.hpp>

#include "fixtures.hpp"

int main(int argc, char** argv) {
    const unsigned hw = std::thread::hardware_concurrency();
    const unsigned max_threads = argc > 1 ? std::stoul(argv[1]) : hw;
    const unsigned repeats = argc > 2 ? std::stoul(argv[2]) : 20;

    auto cfg = gemreco::load_config_csv(fixtures::dir());
    auto raw_events_map = fixtures::load_raw_events();
    std::vector<const gemreco::RawEvent*> events;
    for (const auto& [evt, ev] : raw_events_map) events.push_back(&ev);

    gemreco::PedestalBuilder ped_builder(cfg);
    gemreco::SampleAnalyzer sample_analyzer(cfg);
    for (const auto* ev : events) {
        ped_builder.process_event(gemreco::group_by_apv(*ev, cfg));
        sample_analyzer.process_event(*ev);
    }
    const gemreco::FrozenCalibration calib{ped_builder.freeze(), sample_analyzer.freeze()};

    std::printf("hardware threads: %u, events: %zu, repeats/thread: %u\n", hw,
                events.size(), repeats);
    std::printf("%8s %14s %12s %10s\n", "threads", "events/s", "wall [s]", "speedup");

    double base_rate = 0;
    for (unsigned n = 1; n <= max_threads; n *= 2) {
        auto t0 = std::chrono::steady_clock::now();
        std::vector<std::thread> threads;
        for (unsigned t = 0; t < n; t++) {
            threads.emplace_back([&]() {
                gemreco::Workspace ws;
                for (unsigned r = 0; r < repeats; r++) {
                    for (const auto* ev : events) {
                        volatile auto res =
                            gemreco::reconstruct(*ev, cfg, calib, ws, "GEMTR1X", "GEMTR1Y")
                                .clusters.size();
                        (void)res;
                    }
                }
            });
        }
        for (auto& th : threads) th.join();
        double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        double rate = double(n) * repeats * events.size() / wall;
        if (n == 1) base_rate = rate;
        std::printf("%8u %14.0f %12.2f %9.1fx\n", n, rate, wall, rate / base_rate);
        if (n == max_threads) break;
        if (n * 2 > max_threads && n != max_threads) {
            n = max_threads / 2;  // ensure last iteration hits max_threads
        }
    }
    return 0;
}
