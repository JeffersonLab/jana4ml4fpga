#include "peak_find.hpp"

#include <extmath/PeakFinder.h>

#include "../constants.hpp"

namespace gemreco {

std::vector<PlanePeak> find_plane_peaks(const PlanesDecoded& planes, const Config& cfg) {
    std::vector<PlanePeak> result;
    const auto& p = cfg.params;

    for (const auto& [plane_name, plane_data] : planes) {
        auto cfg_it = cfg.planes.find(plane_name);
        if (cfg_it == cfg.planes.end()) continue;
        const auto& plane_info = cfg_it->second;
        const int apv_num = static_cast<int>(plane_info.apv_ids.size());

        auto peaks = ml4fpga::extmath::find_common_peaks(
            plane_data.data, plane_data.noises, p.peak_n_sigmas, p.peak_min_width,
            p.peak_min_distance, p.peak_time_tolerance);

        for (const auto& peak : peaks) {
            PlanePeak out;
            out.plane_id = 0;
            for (const auto& [apv_id, info] : cfg.apvs) {
                if (info.plane_name == plane_name) { out.plane_id = info.plane_id; break; }
            }
            out.plane_name = plane_name;
            out.index = peak.index;
            out.width = peak.width;
            out.area = peak.area;
            out.height = peak.height;
            out.time_id = peak.time_index;
            // Legacy 2-APV attribution heuristic (PeakFactory.h:87)
            out.apv_id = (peak.index >= kChannelsCount && apv_num > 1) ? plane_info.apv_ids[1]
                                                                       : plane_info.apv_ids[0];
            out.real_pos = strip_position(peak.index, plane_info.size_mm, apv_num);
            result.push_back(std::move(out));
        }
    }
    return result;
}

}  // namespace gemreco
