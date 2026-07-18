// Native .cfg loader parity: load_config_mapping_cfg(2026_mapping_PS.cfg) must
// produce the same Config as load_config_csv (fixture CSVs dumped from the legacy
// GemMapping at capture time). Cfg path via GEMRECO_MAPPING_CFG env var.
#include "catch_amalgamated.hpp"

#include <gemreco/config.hpp>

#include "fixtures.hpp"

TEST_CASE("native cfg loader matches legacy-dumped fixture config", "[config][golden]") {
    const char* cfg_path = std::getenv("GEMRECO_MAPPING_CFG");
    if (!cfg_path) {
        WARN("GEMRECO_MAPPING_CFG not set - skipping native loader parity test");
        return;
    }

    auto native = gemreco::load_config_mapping_cfg(cfg_path);
    auto golden = gemreco::load_config_csv(fixtures::dir());

    REQUIRE(native.apvs.size() == golden.apvs.size());
    for (const auto& [apv_id, g] : golden.apvs) {
        INFO("apv_id=" << apv_id);
        REQUIRE(native.apvs.count(apv_id));
        const auto& n = native.apvs.at(apv_id);
        REQUIRE(n.plane_name == g.plane_name);
        REQUIRE(n.plane_id == g.plane_id);
        REQUIRE(n.detector_name == g.detector_name);
        REQUIRE(n.detector_id == g.detector_id);
        REQUIRE(n.index_on_plane == g.index_on_plane);
        REQUIRE(n.orientation == g.orientation);
        REQUIRE(n.sample_plane_id == g.sample_plane_id);
        REQUIRE(n.sample_detector_id == g.sample_detector_id);
    }

    REQUIRE(native.planes.size() == golden.planes.size());
    for (const auto& [name, g] : golden.planes) {
        INFO("plane=" << name);
        REQUIRE(native.planes.count(name));
        const auto& n = native.planes.at(name);
        REQUIRE(n.size_mm == g.size_mm);
        REQUIRE(n.apv_ids == g.apv_ids);
    }
}
