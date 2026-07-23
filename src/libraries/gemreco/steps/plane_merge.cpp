#include "plane_merge.hpp"

#include "../calibration.hpp"
#include "../constants.hpp"

namespace gemreco {

PlanesDecoded merge_planes(const std::map<int, ApvDecoded>& decoded,
                           const Pedestals& pedestals, const Config& cfg) {
    PlanesDecoded result;

    for (const auto& [plane_name, plane] : cfg.planes) {
        // Find the event's time-bin count from any APV of this plane that has data
        size_t n_timebins = 0;
        for (int apv_id : plane.apv_ids) {
            auto it = decoded.find(apv_id);
            if (it != decoded.end() && !it->second.data.empty()) {
                n_timebins = it->second.data.size();
                break;
            }
        }

        PlaneDecoded merged;
        if (n_timebins == 0) {
            // Legacy: keep an (empty) record so plane_data.at(name) does not throw
            result[plane_name] = std::move(merged);
            continue;
        }

        merged.data.assign(n_timebins, {});
        for (int apv_id : plane.apv_ids) {
            auto it = decoded.find(apv_id);
            const bool ok = it != decoded.end() && it->second.data.size() == n_timebins;

            // data columns
            for (size_t t = 0; t < n_timebins; t++) {
                if (ok) {
                    const auto& row = it->second.data[t];
                    merged.data[t].insert(merged.data[t].end(), row.begin(), row.end());
                } else {
                    merged.data[t].insert(merged.data[t].end(), kChannelsCount, 0.0);
                }
            }

            // noises columns (legacy: AdcDecodedData.PedestalNoises = this APV's noises)
            auto ped_it = pedestals.apvs.find(apv_id);
            if (ok && ped_it != pedestals.apvs.end()) {
                merged.noises.insert(merged.noises.end(), ped_it->second.noises.begin(),
                                     ped_it->second.noises.end());
            } else {
                merged.noises.insert(merged.noises.end(), kChannelsCount, 0.0);
            }
        }
        result[plane_name] = std::move(merged);
    }
    return result;
}

}  // namespace gemreco
