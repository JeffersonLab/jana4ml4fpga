// Stage 4 golden parity: gemreco::merge_planes vs legacy PlaneDecodedDataFactory.
// Golden: plane_decoded.csv ("evt_seq,plane,timebin,values") and
//         plane_noise.csv   ("evt_seq,plane,noises").
#include "catch_amalgamated.hpp"

#include <gemreco/calibration.hpp>
#include <gemreco/config.hpp>
#include <gemreco/steps/apv_decode.hpp>
#include <gemreco/steps/decode_apv.hpp>
#include <gemreco/steps/plane_merge.hpp>

#include "fixtures.hpp"

namespace {

// evt -> plane -> timebin -> values
using GoldenData = std::map<size_t, std::map<std::string, std::map<size_t, std::vector<double>>>>;
// evt -> plane -> noises
using GoldenNoise = std::map<size_t, std::map<std::string, std::vector<double>>>;

GoldenData load_plane_decoded() {
    std::ifstream f(fixtures::dir() + "/plane_decoded.csv");
    REQUIRE(f);
    GoldenData out;
    std::string line;
    std::getline(f, line);
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto c = fixtures::split(line, ',');
        REQUIRE(c.size() == 4);
        out[std::stoul(c[0])][c[1]][std::stoul(c[2])] = fixtures::parse_doubles(c[3]);
    }
    return out;
}

GoldenNoise load_plane_noise() {
    std::ifstream f(fixtures::dir() + "/plane_noise.csv");
    REQUIRE(f);
    GoldenNoise out;
    std::string line;
    std::getline(f, line);
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto c = fixtures::split(line, ',');
        REQUIRE(c.size() == 3);
        out[std::stoul(c[0])][c[1]] = fixtures::parse_doubles(c[2]);
    }
    return out;
}

}  // namespace

TEST_CASE("merge_planes reproduces legacy PlaneDecodedData bit-for-bit", "[plane_merge][golden]") {
    auto cfg = gemreco::load_config_csv(fixtures::dir());
    auto raw_events = fixtures::load_raw_events();
    auto golden_data = load_plane_decoded();
    auto golden_noise = load_plane_noise();
    REQUIRE(!golden_data.empty());

    gemreco::PedestalBuilder builder(cfg);
    size_t compared_values = 0;

    for (const auto& [evt, raw_event] : raw_events) {
        auto grouped = gemreco::group_by_apv(raw_event, cfg);
        auto peds = builder.process_event(grouped);

        auto it = golden_data.find(evt);
        if (it == golden_data.end()) continue;

        // Decode all APVs of the event (legacy ApvDecodedDataFactory does all)
        std::map<int, gemreco::ApvDecoded> decoded;
        for (const auto& [apv_id, block] : grouped) {
            if (!peds.apvs.count(apv_id)) continue;
            auto d = gemreco::decode_apv_adc(block, peds.apvs.at(apv_id), cfg.params);
            if (d.data.empty()) continue;  // legacy skips malformed APVs
            decoded[apv_id] = std::move(d);
        }

        auto planes = gemreco::merge_planes(decoded, peds, cfg);

        for (const auto& [plane_name, timebins] : it->second) {
            INFO("evt_seq=" << evt << " plane=" << plane_name);
            REQUIRE(planes.count(plane_name));
            const auto& merged = planes.at(plane_name);
            for (const auto& [t, values] : timebins) {
                INFO("timebin " << t);
                REQUIRE(merged.data.size() > t);
                REQUIRE(merged.data[t] == values);  // exact doubles
                compared_values += values.size();
            }
            // noises
            const auto& gn = golden_noise.at(evt).at(plane_name);
            REQUIRE(merged.noises == gn);
            compared_values += gn.size();
        }
    }
    REQUIRE(compared_values > 10000);
    WARN("plane_merge golden parity: compared " << compared_values << " values");
}
