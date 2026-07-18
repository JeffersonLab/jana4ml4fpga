// Stage 3 golden parity: gemreco::decode_apv_adc vs legacy ApvDecodedDataFactory.
// Golden: apv_decoded.csv rows "evt_seq,apv_id,timebin,common_mode,raw_avg,values".
// The pedestal evolves per event, so the test replays events sequentially through
// PedestalBuilder (already proven bit-for-bit in pedestal_tests).
#include "catch_amalgamated.hpp"

#include <gemreco/calibration.hpp>
#include <gemreco/config.hpp>
#include <gemreco/steps/apv_decode.hpp>
#include <gemreco/steps/decode_apv.hpp>

#include "fixtures.hpp"

namespace {

struct GoldenRow {
    double common_mode;
    double raw_avg;
    std::vector<double> values;
};
// evt -> apv -> timebin -> row
using Golden = std::map<size_t, std::map<int, std::map<size_t, GoldenRow>>>;

Golden load_apv_decoded() {
    std::ifstream f(fixtures::dir() + "/apv_decoded.csv");
    REQUIRE(f);
    Golden out;
    std::string line;
    std::getline(f, line);  // header
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto c = fixtures::split(line, ',');
        REQUIRE(c.size() == 6);
        GoldenRow row;
        row.common_mode = std::stod(c[3]);
        row.raw_avg = std::stod(c[4]);
        row.values = fixtures::parse_doubles(c[5]);
        out[std::stoul(c[0])][std::stoi(c[1])][std::stoul(c[2])] = std::move(row);
    }
    return out;
}

}  // namespace

TEST_CASE("decode_apv_adc reproduces legacy DecodeApv bit-for-bit", "[apv_decode][golden]") {
    auto cfg = gemreco::load_config_csv(fixtures::dir());
    auto raw_events = fixtures::load_raw_events();
    auto golden = load_apv_decoded();
    REQUIRE(!golden.empty());

    gemreco::PedestalBuilder builder(cfg);
    size_t compared_values = 0;

    for (const auto& [evt, raw_event] : raw_events) {
        auto grouped = gemreco::group_by_apv(raw_event, cfg);
        auto peds = builder.process_event(grouped);

        auto it = golden.find(evt);
        if (it == golden.end()) continue;

        for (const auto& [apv_id, timebins] : it->second) {
            INFO("evt_seq=" << evt << " apv_id=" << apv_id);
            REQUIRE(grouped.count(apv_id));
            REQUIRE(peds.apvs.count(apv_id));
            auto decoded = gemreco::decode_apv_adc(grouped.at(apv_id), peds.apvs.at(apv_id),
                                                   cfg.params);
            REQUIRE(decoded.data.size() >= timebins.rbegin()->first + 1);

            for (const auto& [t, row] : timebins) {
                INFO("timebin " << t);
                REQUIRE(decoded.common_mode_offsets[t] == row.common_mode);
                REQUIRE(decoded.raw_data_average[t] == row.raw_avg);
                REQUIRE(decoded.data[t].size() == row.values.size());
                for (size_t ch = 0; ch < row.values.size(); ch++) {
                    if (decoded.data[t][ch] != row.values[ch]) {
                        INFO("channel " << ch);
                        REQUIRE(decoded.data[t][ch] == row.values[ch]);
                    }
                }
                compared_values += row.values.size() + 2;
            }
        }
    }
    REQUIRE(compared_values > 10000);
    WARN("apv_decode golden parity: compared " << compared_values << " values");
}
