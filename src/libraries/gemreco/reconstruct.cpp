#include "reconstruct.hpp"

#include "steps/decode_apv.hpp"

namespace gemreco {

RecoResult reconstruct(const RawEvent& event, const Config& cfg,
                       const FrozenCalibration& calib, Workspace& ws,
                       const std::string& plane_x, const std::string& plane_y) {
    ws.reset();
    RecoResult out;

    // stage 1: group by APV
    auto grouped = group_by_apv(event, cfg);

    // stage 3: decode each APV against the frozen pedestal
    for (const auto& [apv_id, block] : grouped) {
        auto ped_it = calib.pedestals.apvs.find(apv_id);
        if (ped_it == calib.pedestals.apvs.end()) continue;
        auto decoded = decode_apv_adc(block, ped_it->second, cfg.params);
        if (decoded.data.empty()) continue;  // malformed APV, zero-substituted later
        ws.decoded[apv_id] = std::move(decoded);
    }

    // stage 4-7
    out.planes = merge_planes(ws.decoded, calib.pedestals, cfg);
    out.peaks = find_plane_peaks(out.planes, cfg);
    out.clusters = match_clusters(out.planes, cfg, plane_x, plane_y);
    out.samples = analyze_samples_frozen(event, cfg, calib.sample_baselines);

    return out;
}

}  // namespace gemreco
