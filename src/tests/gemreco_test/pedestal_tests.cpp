// Stage 2 golden parity: gemreco::PedestalBuilder vs legacy PedestalFactory.
// Replays ALL fixture events in order (the pedestal is a running statistic),
// comparing offsets/noises bit-for-bit at every checkpoint event present in
// pedestal.csv.
#include "catch_amalgamated.hpp"

#include <gemreco/calibration.hpp>
#include <gemreco/config.hpp>
#include <gemreco/steps/decode_apv.hpp>

#include "fixtures.hpp"

TEST_CASE("PedestalBuilder reproduces legacy PedestalFactory bit-for-bit", "[pedestal][golden]") {
    auto cfg = gemreco::load_config_csv(fixtures::dir());
    auto raw_events = fixtures::load_raw_events();
    auto golden = fixtures::load_per_apv("pedestal.csv");  // evt -> apv -> {offsets, noises}

    REQUIRE(!golden.empty());
    REQUIRE(!raw_events.empty());

    gemreco::PedestalBuilder builder(cfg);
    size_t compared_values = 0;

    // Sequential replay in evt_seq order (std::map iteration is ordered)
    for (const auto& [evt, raw_event] : raw_events) {
        auto grouped = gemreco::group_by_apv(raw_event, cfg);
        auto peds = builder.process_event(grouped);

        auto it = golden.find(evt);
        if (it == golden.end()) continue;  // not a checkpoint

        for (const auto& [apv_id, cols] : it->second) {
            INFO("evt_seq=" << evt << " apv_id=" << apv_id);
            REQUIRE(peds.apvs.count(apv_id));
            const auto& expected_offsets = cols.at(0);
            const auto& expected_noises = cols.at(1);
            const auto& actual = peds.apvs.at(apv_id);
            REQUIRE(actual.offsets.size() == expected_offsets.size());
            REQUIRE(actual.noises.size() == expected_noises.size());
            for (size_t ch = 0; ch < expected_offsets.size(); ch++) {
                if (actual.offsets[ch] != expected_offsets[ch]
                    || actual.noises[ch] != expected_noises[ch]) {
                    INFO("channel " << ch);
                    REQUIRE(actual.offsets[ch] == expected_offsets[ch]);
                    REQUIRE(actual.noises[ch] == expected_noises[ch]);
                }
            }
            compared_values += expected_offsets.size() + expected_noises.size();
        }
    }
    REQUIRE(compared_values > 10000);
    WARN("pedestal golden parity: compared " << compared_values << " values");
}

TEST_CASE("PedestalBuilder freeze() is stable and immutable", "[pedestal][freeze]") {
    auto cfg = gemreco::load_config_csv(fixtures::dir());
    auto raw_events = fixtures::load_raw_events();

    gemreco::PedestalBuilder builder(cfg);
    for (const auto& [evt, raw_event] : raw_events) {
        builder.process_event(gemreco::group_by_apv(raw_event, cfg));
    }

    auto frozen1 = builder.freeze();
    auto frozen2 = builder.freeze();
    REQUIRE(!frozen1.apvs.empty());
    // freeze() is const: two snapshots are identical
    for (const auto& [apv_id, ped] : frozen1.apvs) {
        REQUIRE(frozen2.apvs.at(apv_id).offsets == ped.offsets);
        REQUIRE(frozen2.apvs.at(apv_id).noises == ped.noises);
    }
}
