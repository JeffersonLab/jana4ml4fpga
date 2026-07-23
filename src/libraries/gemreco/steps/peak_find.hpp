// gemreco stage 5: peak finding on merged plane data.
// Faithful port of the legacy PeakFactory (plugins/gemrecon) - a thin wrapper
// over extmath::find_common_peaks plus plane/APV attribution.
#pragma once

#include <string>
#include <vector>

#include "../config.hpp"
#include "plane_merge.hpp"

namespace gemreco {

struct PlanePeak {
    int plane_id = -1;
    std::string plane_name;
    int time_id = 0;      ///< time bin with the peak maximum
    int index = -1;       ///< strip index within the plane
    int apv_id = -1;      ///< APV attributed to this strip (legacy 2-APV heuristic)
    double height = 0;
    int width = 0;
    double area = 0;
    double real_pos = 0;  ///< position [mm] from plane geometry
};

/// Find peaks on every plane. Output order matches the legacy factory:
/// planes in map (alphabetical) order, peaks in extmath detection order.
std::vector<PlanePeak> find_plane_peaks(const PlanesDecoded& planes, const Config& cfg);

}  // namespace gemreco
