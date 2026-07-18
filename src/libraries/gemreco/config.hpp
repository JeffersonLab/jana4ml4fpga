// gemreco: immutable configuration shared by all threads.
// Built once by a loader (inversion of control - nothing here self-loads),
// then passed around as `const Config&` / `shared_ptr<const Config>`.
// Replaces the legacy GemMapping singleton with plain, non-mutating data.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace gemreco {

struct ApvInfo {
    int apv_id = -1;
    std::string plane_name;
    int plane_id = -1;
    std::string detector_name;
    int detector_id = -1;
    int index_on_plane = 0;
    int orientation = 0;
    // Values the legacy SampleAnalysisFactory uses for sample ids (bit-parity):
    // GetPlaneID(plane) = cartesian axis (X=0/Y=1); sample_detector_id is 0 with
    // legacy mappings because GemMapping::fDetectorFromAPVIDMap is never populated.
    int sample_plane_id = 0;
    int sample_detector_id = 0;
};

struct PlaneInfo {
    std::string name;
    double size_mm = 0;
    std::vector<int> apv_ids;  ///< ordered by index_on_plane
};

/// Algorithm parameters. Defaults = the values hardcoded in the legacy factories.
struct AlgoParams {
    // apv decode (legacy ApvDecodedDataFactory::DecodeApv)
    int common_mode_cut = 20;
    double zero_sup_cut = 10;
    int apv_baseline = 2500;
    // pedestal (legacy PedestalFactory)
    int pedestal_events_to_average = 500;
    int pedestal_minimal_events = 15;
    double pedestal_constant_offset = 3000;
    double pedestal_constant_noise = 0;
    // peak finding (legacy PeakFactory/ClusterFactory)
    double peak_n_sigmas = 3.0;
    int peak_min_width = 2;
    int peak_min_distance = 2;
    int peak_time_tolerance = 2;
};

struct Config {
    std::map<int, ApvInfo> apvs;
    std::map<std::string, PlaneInfo> planes;
    AlgoParams params;

    bool has_apv(int apv_id) const { return apvs.count(apv_id) != 0; }
};

/// Load a Config from the fixture CSV pair written by the gemreco_fixdump plugin
/// (mapping_apvs.csv + mapping_planes.csv in `dir`). This is loader v1 used by
/// unit tests; a native parser for the legacy .cfg format comes with the JANA
/// adapter phase.
Config load_config_csv(const std::string& dir);

/// Load a Config directly from the legacy SRS mapping .cfg file (DET/APV rows,
/// CARTESIAN readout). Verified against load_config_csv fixtures in unit tests.
Config load_config_mapping_cfg(const std::string& path);

}  // namespace gemreco
