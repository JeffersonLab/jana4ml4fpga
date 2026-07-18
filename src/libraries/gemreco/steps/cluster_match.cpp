#include "cluster_match.hpp"

#include <extmath/PeakFinder.h>

#include "../constants.hpp"

namespace gemreco {

std::vector<Cluster> match_clusters(const PlanesDecoded& planes, const Config& cfg,
                                    const std::string& plane_x, const std::string& plane_y) {
    std::vector<Cluster> result;

    auto px = planes.find(plane_x);
    auto py = planes.find(plane_y);
    auto cx = cfg.planes.find(plane_x);
    auto cy = cfg.planes.find(plane_y);
    if (px == planes.end() || py == planes.end() || cx == cfg.planes.end()
        || cy == cfg.planes.end()) {
        return result;  // legacy: .at() throws -> caught -> no clusters
    }

    const auto& p = cfg.params;
    auto peaks_x = ml4fpga::extmath::find_common_peaks(
        px->second.data, px->second.noises, p.peak_n_sigmas, p.peak_min_width,
        p.peak_min_distance, p.peak_time_tolerance);
    auto peaks_y = ml4fpga::extmath::find_common_peaks(
        py->second.data, py->second.noises, p.peak_n_sigmas, p.peak_min_width,
        p.peak_min_distance, p.peak_time_tolerance);

    auto matched = ml4fpga::extmath::match_peaks(peaks_x, peaks_y,
                                                 ml4fpga::extmath::PeakFindingMode::AUTO);

    const int apv_num_x = static_cast<int>(cx->second.apv_ids.size());
    const int apv_num_y = static_cast<int>(cy->second.apv_ids.size());

    for (const auto& pair : matched) {
        Cluster c;
        c.index_x = pair.x_data.index;
        c.index_y = pair.y_data.index;
        c.pos_x = strip_position(pair.x_data.index, cx->second.size_mm, apv_num_x);
        c.pos_y = strip_position(pair.y_data.index, cy->second.size_mm, apv_num_y);
        // LEGACY QUIRK (preserved): amplitude assigned x.height then OVERWRITTEN with
        // y.height (ClusterFactory.cpp:112-113); energy = y peak WIDTH (:114).
        c.amplitude = pair.y_data.height;
        c.energy = pair.y_data.width;
        result.push_back(c);
    }
    return result;
}

}  // namespace gemreco
