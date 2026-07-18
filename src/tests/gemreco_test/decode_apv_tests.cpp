// Stage 1 golden parity: gemreco::group_by_apv vs legacy RawDataFactory output.
// Golden fixtures: raw_data.csv (checkpoint events) from gemreco_fixdump.
#include "catch_amalgamated.hpp"

#include <gemreco/config.hpp>
#include <gemreco/steps/decode_apv.hpp>

#include "fixtures.hpp"

TEST_CASE("group_by_apv reproduces legacy RawData bit-for-bit", "[decode_apv][golden]") {
    auto cfg = gemreco::load_config_csv(fixtures::dir());
    auto raw_events = fixtures::load_raw_events();
    auto golden = fixtures::load_per_apv("raw_data.csv");  // evt -> apv -> {all_samples}

    REQUIRE(!golden.empty());
    size_t compared_values = 0;

    for (const auto& [evt, apv_golden] : golden) {
        REQUIRE(raw_events.count(evt));
        auto grouped = gemreco::group_by_apv(raw_events.at(evt), cfg);

        // Same APV set
        REQUIRE(grouped.size() == apv_golden.size());

        for (const auto& [apv_id, cols] : apv_golden) {
            INFO("evt_seq=" << evt << " apv_id=" << apv_id);
            REQUIRE(grouped.count(apv_id));
            const auto& expected = cols.at(0);  // all_samples column
            const auto& actual = grouped.at(apv_id).all_samples;
            REQUIRE(actual.size() == expected.size());
            for (size_t i = 0; i < expected.size(); i++) {
                if (actual[i] != expected[i]) {  // exact: values are integer-valued doubles
                    INFO("index " << i);
                    REQUIRE(actual[i] == expected[i]);
                }
            }
            compared_values += expected.size();
        }
    }
    // Guard against silently comparing nothing
    REQUIRE(compared_values > 10000);
    WARN("decode_apv golden parity: compared " << compared_values << " values");
}
