// Stage 6 golden parity: gemreco::match_clusters vs legacy ClusterFactory.
// Golden: clusters.csv "evt_seq,index_x,index_y,pos_x,pos_y,amplitude,energy".
// Fixtures were captured with gemrecon:plane_name_x/_y = GEMTR1X/GEMTR1Y.
#include "catch_amalgamated.hpp"

#include <gemreco/calibration.hpp>
#include <gemreco/config.hpp>
#include <gemreco/steps/apv_decode.hpp>
#include <gemreco/steps/cluster_match.hpp>
#include <gemreco/steps/decode_apv.hpp>
#include <gemreco/steps/plane_merge.hpp>

#include "fixtures.hpp"

namespace {
const char* kPlaneX = "GEMTR1X";
const char* kPlaneY = "GEMTR1Y";

// evt -> clusters (row = 6 doubles)
std::map<size_t, std::vector<std::vector<double>>> load_clusters() {
    std::ifstream f(fixtures::dir() + "/clusters.csv");
    REQUIRE(f);
    std::map<size_t, std::vector<std::vector<double>>> out;
    std::string line;
    std::getline(f, line);
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto c = fixtures::split(line, ',');
        REQUIRE(c.size() == 7);
        std::vector<double> row;
        for (size_t i = 1; i < 7; i++) row.push_back(std::stod(c[i]));
        out[std::stoul(c[0])].push_back(std::move(row));
    }
    return out;
}
}  // namespace

TEST_CASE("match_clusters reproduces legacy ClusterFactory bit-for-bit", "[cluster_match][golden]") {
    auto cfg = gemreco::load_config_csv(fixtures::dir());
    auto raw_events = fixtures::load_raw_events();
    auto golden = load_clusters();

    gemreco::PedestalBuilder builder(cfg);
    size_t compared_clusters = 0;

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
        auto clusters = gemreco::match_clusters(planes, cfg, kPlaneX, kPlaneY);

        auto it = golden.find(evt);
        const size_t n_expected = (it != golden.end()) ? it->second.size() : 0;
        INFO("evt_seq=" << evt);
        REQUIRE(clusters.size() == n_expected);

        for (size_t i = 0; i < n_expected; i++) {
            const auto& e = it->second[i];
            INFO("cluster " << i);
            REQUIRE(clusters[i].index_x == e[0]);
            REQUIRE(clusters[i].index_y == e[1]);
            REQUIRE(clusters[i].pos_x == e[2]);
            REQUIRE(clusters[i].pos_y == e[3]);
            REQUIRE(clusters[i].amplitude == e[4]);
            REQUIRE(clusters[i].energy == e[5]);
            compared_clusters++;
        }
    }
    WARN("cluster_match golden parity: " << compared_clusters << " clusters (sparse pulser data)");
}
