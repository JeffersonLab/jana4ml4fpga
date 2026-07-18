// Stage 5 golden parity: gemreco::find_plane_peaks vs legacy PeakFactory.
// Golden: peaks.csv "evt_seq,plane_name,plane_id,apv_id,time_id,index,width,height,area,real_pos"
// Peaks are dumped for EVERY event (sparse), so this test replays all events and
// requires exact match of the full peak list per event.
#include "catch_amalgamated.hpp"

#include <gemreco/calibration.hpp>
#include <gemreco/config.hpp>
#include <gemreco/steps/apv_decode.hpp>
#include <gemreco/steps/decode_apv.hpp>
#include <gemreco/steps/peak_find.hpp>
#include <gemreco/steps/plane_merge.hpp>

#include "fixtures.hpp"

namespace {

struct GoldenPeak {
    std::string plane_name;
    int plane_id, apv_id, time_id, index, width;
    double height, area, real_pos;
};

std::map<size_t, std::vector<GoldenPeak>> load_peaks() {
    std::ifstream f(fixtures::dir() + "/peaks.csv");
    REQUIRE(f);
    std::map<size_t, std::vector<GoldenPeak>> out;
    std::string line;
    std::getline(f, line);
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto c = fixtures::split(line, ',');
        REQUIRE(c.size() == 10);
        GoldenPeak p;
        p.plane_name = c[1];
        p.plane_id = std::stoi(c[2]);
        p.apv_id = std::stoi(c[3]);
        p.time_id = std::stoi(c[4]);
        p.index = std::stoi(c[5]);
        p.width = std::stoi(c[6]);
        p.height = std::stod(c[7]);
        p.area = std::stod(c[8]);
        p.real_pos = std::stod(c[9]);
        out[std::stoul(c[0])].push_back(std::move(p));
    }
    return out;
}

}  // namespace

TEST_CASE("find_plane_peaks reproduces legacy PeakFactory bit-for-bit", "[peak_find][golden]") {
    auto cfg = gemreco::load_config_csv(fixtures::dir());
    auto raw_events = fixtures::load_raw_events();
    auto golden = load_peaks();
    REQUIRE(!golden.empty());

    gemreco::PedestalBuilder builder(cfg);
    size_t compared_peaks = 0, events_with_peaks = 0;

    for (const auto& [evt, raw_event] : raw_events) {
        auto grouped = gemreco::group_by_apv(raw_event, cfg);
        auto peds = builder.process_event(grouped);

        std::map<int, gemreco::ApvDecoded> decoded;
        for (const auto& [apv_id, block] : grouped) {
            if (!peds.apvs.count(apv_id)) continue;
            auto d = gemreco::decode_apv_adc(block, peds.apvs.at(apv_id), cfg.params);
            if (d.data.empty()) continue;
            decoded[apv_id] = std::move(d);
        }
        auto planes = gemreco::merge_planes(decoded, peds, cfg);
        auto peaks = gemreco::find_plane_peaks(planes, cfg);

        auto it = golden.find(evt);
        const auto& expected = (it != golden.end()) ? it->second : std::vector<GoldenPeak>{};
        INFO("evt_seq=" << evt);
        REQUIRE(peaks.size() == expected.size());
        if (!expected.empty()) events_with_peaks++;

        for (size_t i = 0; i < expected.size(); i++) {
            INFO("peak " << i << " plane=" << expected[i].plane_name);
            REQUIRE(peaks[i].plane_name == expected[i].plane_name);
            REQUIRE(peaks[i].plane_id == expected[i].plane_id);
            REQUIRE(peaks[i].apv_id == expected[i].apv_id);
            REQUIRE(peaks[i].time_id == expected[i].time_id);
            REQUIRE(peaks[i].index == expected[i].index);
            REQUIRE(peaks[i].width == expected[i].width);
            REQUIRE(peaks[i].height == expected[i].height);
            REQUIRE(peaks[i].area == expected[i].area);
            REQUIRE(peaks[i].real_pos == expected[i].real_pos);
            compared_peaks++;
        }
    }
    REQUIRE(compared_peaks > 5);
    WARN("peak_find golden parity: " << compared_peaks << " peaks over "
                                     << events_with_peaks << " events with peaks");
}
