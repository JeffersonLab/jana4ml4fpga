// Stage 7 golden parity: gemreco::SampleAnalyzer vs legacy SampleAnalysisFactory.
// Golden: samples.csv (checkpoint events)
// "evt_seq,id,channel,raw_channel,time_bin,apv,plane,detector,is_noise,value,raw_value,rolling_average,rolling_std"
// Rolling stats are cross-event state, so all events replay sequentially.
#include "catch_amalgamated.hpp"

#include <gemreco/config.hpp>
#include <gemreco/steps/rolling_baseline.hpp>

#include "fixtures.hpp"

namespace {

struct GoldenSample {
    uint64_t id;
    uint32_t channel, raw_channel, time_bin, apv, plane, detector;
    bool is_noise;
    double value, raw_value, rolling_average, rolling_std;
};

std::map<size_t, std::vector<GoldenSample>> load_samples() {
    std::ifstream f(fixtures::dir() + "/samples.csv");
    REQUIRE(f);
    std::map<size_t, std::vector<GoldenSample>> out;
    std::string line;
    std::getline(f, line);
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto c = fixtures::split(line, ',');
        REQUIRE(c.size() == 13);
        GoldenSample s;
        s.id = std::stoull(c[1]);
        s.channel = std::stoul(c[2]);
        s.raw_channel = std::stoul(c[3]);
        s.time_bin = std::stoul(c[4]);
        s.apv = std::stoul(c[5]);
        s.plane = std::stoul(c[6]);
        s.detector = std::stoul(c[7]);
        s.is_noise = c[8] == "1";
        s.value = std::stod(c[9]);
        s.raw_value = std::stod(c[10]);
        s.rolling_average = std::stod(c[11]);
        s.rolling_std = std::stod(c[12]);
        out[std::stoul(c[0])].push_back(std::move(s));
    }
    return out;
}

}  // namespace

TEST_CASE("SampleAnalyzer reproduces legacy SampleAnalysisFactory bit-for-bit",
          "[rolling_baseline][golden]") {
    auto cfg = gemreco::load_config_csv(fixtures::dir());
    auto raw_events = fixtures::load_raw_events();
    auto golden = load_samples();
    REQUIRE(!golden.empty());

    gemreco::SampleAnalyzer analyzer(cfg);  // defaults = legacy defaults
    size_t compared_samples = 0;

    for (const auto& [evt, raw_event] : raw_events) {
        auto samples = analyzer.process_event(raw_event);

        auto it = golden.find(evt);
        if (it == golden.end()) continue;

        INFO("evt_seq=" << evt);
        REQUIRE(samples.size() == it->second.size());
        for (size_t i = 0; i < samples.size(); i++) {
            const auto& a = samples[i];
            const auto& e = it->second[i];
            INFO("sample " << i << " id=" << e.id);
            REQUIRE(a.id == e.id);
            REQUIRE(a.channel == e.channel);
            REQUIRE(a.raw_channel == e.raw_channel);
            REQUIRE(a.time_bin == e.time_bin);
            REQUIRE(a.apv == e.apv);
            REQUIRE(a.plane == e.plane);
            REQUIRE(a.detector == e.detector);
            REQUIRE(a.is_noise == e.is_noise);
            REQUIRE(a.value == e.value);
            REQUIRE(a.raw_value == e.raw_value);
            REQUIRE(a.rolling_average == e.rolling_average);
            REQUIRE(a.rolling_std == e.rolling_std);
            compared_samples++;
        }
    }
    REQUIRE(compared_samples > 10000);
    WARN("rolling_baseline golden parity: compared " << compared_samples << " samples");
}
