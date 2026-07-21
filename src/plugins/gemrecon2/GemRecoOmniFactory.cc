#include "GemRecoOmniFactory.h"

#include <gemreco/workspace.hpp>

void GemRecoOmniFactory::Execute(int32_t /*run_nr*/, uint64_t event_nr) {
    // --- marshal EVIO -> library input -----------------------------------------
    gemreco::RawEvent raw;
    raw.event_number = event_nr;
    raw.srs.reserve(m_srs_in().size());
    for (const auto* hit : m_srs_in()) {
        gemreco::SrsChannelData ch;
        ch.apv_id = static_cast<int>(hit->apv_id);
        ch.raw_channel_apv = static_cast<int>(hit->channel_apv);
        ch.samples.assign(hit->samples.begin(), hit->samples.end());
        raw.srs.push_back(std::move(ch));
    }

    // --- reconstruct (per-thread workspace) ------------------------------------
    static thread_local gemreco::Workspace ws;
    auto res = m_service->Reconstruct(raw, ws);

    // --- republish as JANA objects ---------------------------------------------
    std::vector<ml4fpga::gem::PlanePeak*> peaks;
    auto* pf_result = new ml4fpga::gem::PlanePeakFindingResult();
    for (const auto& p : res.peaks) {
        auto* out = new ml4fpga::gem::PlanePeak();
        out->plane_id = p.plane_id;
        out->plane_name = p.plane_name;
        out->time_id = p.time_id;
        out->index = p.index;
        out->apv_id = p.apv_id;
        out->height = p.height;
        out->width = p.width;
        out->area = p.area;
        out->real_pos = p.real_pos;
        peaks.push_back(out);
        pf_result->peaks_by_plane[p.plane_name].push_back(*out);
    }

    std::vector<ml4fpga::gem::SFclust*> clusters;
    for (const auto& c : res.clusters) {
        auto* out = new ml4fpga::gem::SFclust();
        out->index_x = c.index_x;
        out->index_y = c.index_y;
        out->pos_x = c.pos_x;
        out->pos_y = c.pos_y;
        out->energy = c.energy;
        out->amplitude = c.amplitude;
        out->N = 0;
        clusters.push_back(out);
    }

    auto* plane_decoded = new ml4fpga::gem::PlaneDecodedData();
    for (auto& [name, pd] : res.planes) {
        ml4fpga::gem::AdcDecodedData d;
        d.data = pd.data;
        d.PedestalNoises = pd.noises;
        d.plane_name = name;
        plane_decoded->plane_data[name] = std::move(d);
    }

    std::vector<ml4fpga::gem::SampleData*> samples;
    samples.reserve(res.samples.size());
    for (const auto& s : res.samples) {
        auto* out = new ml4fpga::gem::SampleData();
        out->id = s.id;
        out->channel = s.channel;
        out->raw_channel = s.raw_channel;
        out->time_bin = s.time_bin;
        out->apv = s.apv;
        out->plane = s.plane;
        out->detector = s.detector;
        out->is_noise = s.is_noise;
        out->value = s.value;
        out->raw_value = s.raw_value;
        out->rolling_average = s.rolling_average;
        out->rolling_std = s.rolling_std;
        samples.push_back(out);
    }

    m_peaks_out() = std::move(peaks);
    m_peak_result_out() = {pf_result};
    m_clusters_out() = std::move(clusters);
    m_plane_data_out() = {plane_decoded};
    m_samples_out() = std::move(samples);
}
