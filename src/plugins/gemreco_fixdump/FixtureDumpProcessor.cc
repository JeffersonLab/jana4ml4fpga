#include "FixtureDumpProcessor.h"

#include <filesystem>
#include <iomanip>

#include <JANA/JEvent.h>

#include <rawdataparser/DGEMSRSWindowRawData.h>
#include <plugins/gemrecon/RawData.h>
#include <plugins/gemrecon/Pedestal.h>
#include <plugins/gemrecon/DecodedData.h>
#include <plugins/gemrecon/PlanePeak.h>
#include <plugins/gemrecon/SFclust.h>
#include <plugins/gemrecon/SampleData.h>
#include <plugins/gemrecon/GemMappingService.h>

namespace {
/// Full-precision double so CSV -> double round-trips bit-for-bit
std::string d2s(double v) {
    std::ostringstream ss;
    ss << std::setprecision(17) << v;
    return ss.str();
}
}  // namespace

void FixtureDumpProcessor::Init() {
    auto app = GetApplication();
    InitLogger(GetPluginName());

    app->SetDefaultParameter("fixdump:dir", m_dir, "Output directory for fixture CSVs");
    app->SetDefaultParameter("fixdump:nevents", m_nevents, "Number of events to dump");

    std::filesystem::create_directories(m_dir);

    auto open = [this](std::ofstream& f, const std::string& name, const std::string& header) {
        f.open(m_dir + "/" + name);
        f << header << "\n";
    };
    open(m_srs_raw, "srs_raw.csv", "evt_seq,event_number,apv_id,channel_apv,samples");
    open(m_raw_data, "raw_data.csv", "evt_seq,apv_id,all_samples");
    open(m_pedestal, "pedestal.csv", "evt_seq,apv_id,offsets,noises");
    open(m_apv_decoded, "apv_decoded.csv", "evt_seq,apv_id,timebin,common_mode,raw_avg,values");
    open(m_plane_decoded, "plane_decoded.csv", "evt_seq,plane,timebin,values");
    open(m_plane_noise, "plane_noise.csv", "evt_seq,plane,noises");
    open(m_peaks, "peaks.csv", "evt_seq,plane_name,plane_id,apv_id,time_id,index,width,height,area,real_pos");
    open(m_clusters, "clusters.csv", "evt_seq,index_x,index_y,pos_x,pos_y,amplitude,energy");
    open(m_samples, "samples.csv",
         "evt_seq,id,channel,raw_channel,time_bin,apv,plane,detector,is_noise,value,raw_value,rolling_average,rolling_std");

    DumpMapping();
    logger()->info("Fixture dump to '{}', nevents={}", m_dir, m_nevents);
}

void FixtureDumpProcessor::DumpMapping() {
    // Requires GemMappingService (loaded by the gemrecon plugin's InitPlugin)
    auto mapping = GetApplication()->GetService<ml4fpga::gem::GemMappingService>()->GetMapping();

    std::ofstream apvs(m_dir + "/mapping_apvs.csv");
    // sample_plane_id/sample_det_id: the EXACT values SampleAnalysisFactory uses for
    // its sample ids - GetPlaneID(plane) (cartesian axis 0/1) and
    // GetDetectorID(GetDetectorFromAPVIDMap()[apv]). The latter map is never filled
    // by LoadMapping, so the id is GetDetectorID("") == 0 - a legacy quirk the new
    // library must reproduce for bit-parity.
    apvs << "apv_id,plane_name,plane_id,det_name,det_id,index_on_plane,orientation,"
            "sample_plane_id,sample_det_id\n";
    for (auto& [apv_id, apv_name] : mapping->GetAPVFromIDMap()) {
        auto plane = mapping->GetPlaneFromAPVID(apv_id);
        auto det = mapping->GetDetectorFromPlane(plane);
        auto sample_det_name = mapping->GetDetectorFromAPVIDMap()[apv_id];
        apvs << apv_id << "," << plane << "," << mapping->GetPlaneIDFromPlane(plane) << ","
             << det << "," << mapping->GetDetectorID(det) << ","
             << mapping->GetAPVIndexOnPlane(apv_id) << ","
             << mapping->GetAPVOrientation(apv_id) << ","
             << mapping->GetPlaneID(plane) << ","
             << mapping->GetDetectorID(sample_det_name) << "\n";
    }

    std::ofstream planes(m_dir + "/mapping_planes.csv");
    planes << "plane_name,size_mm,apv_ids\n";
    for (auto& [plane, apv_list] : mapping->GetAPVIDListFromPlaneMap()) {
        planes << plane << "," << d2s(mapping->GetPlaneSize(plane)) << ",";
        std::string sep;
        for (int id : mapping->GetAPVIDListFromPlane(plane)) {
            planes << sep << id;
            sep = " ";
        }
        planes << "\n";
    }
}

bool FixtureDumpProcessor::IsCheckpoint(size_t evt_seq) const {
    return m_checkpoints.count(evt_seq) != 0;
}

void FixtureDumpProcessor::Process(const std::shared_ptr<const JEvent>& event) {
    // Single-threaded tool by design (fixture replay order must be deterministic)
    size_t evt = m_evt_seq++;
    if (evt >= m_nevents) return;

    // --- raw input: every event ---
    std::vector<const DGEMSRSWindowRawData*> srs;
    try { srs = event->Get<DGEMSRSWindowRawData>(); } catch (...) { return; }
    for (auto hit : srs) {
        m_srs_raw << evt << "," << event->GetEventNumber() << "," << hit->apv_id << ","
                  << hit->channel_apv << ",";
        std::string sep;
        for (auto s : hit->samples) { m_srs_raw << sep << s; sep = " "; }
        m_srs_raw << "\n";
    }

    // --- peaks + clusters: every event (sparse; also promotes event to checkpoint) ---
    bool has_peaks = false;
    try {
        for (auto pk : event->Get<ml4fpga::gem::PlanePeak>()) {
            has_peaks = true;
            m_peaks << evt << "," << pk->plane_name << "," << pk->plane_id << "," << pk->apv_id
                    << "," << pk->time_id << "," << pk->index << "," << pk->width << ","
                    << d2s(pk->height) << "," << d2s(pk->area) << "," << d2s(pk->real_pos) << "\n";
        }
        for (auto cl : event->Get<ml4fpga::gem::SFclust>()) {
            m_clusters << evt << "," << d2s(cl->index_x) << "," << d2s(cl->index_y) << ","
                       << d2s(cl->pos_x) << "," << d2s(cl->pos_y) << ","
                       << d2s(cl->amplitude) << "," << d2s(cl->energy) << "\n";
        }
    } catch (std::exception& e) {
        logger()->warn("evt_seq {}: peaks/clusters unavailable: {}", evt, e.what());
    }

    if (!IsCheckpoint(evt) && !has_peaks) return;

    // --- per-stage intermediates: checkpoints only ---
    try {
        auto raw = event->GetSingle<ml4fpga::gem::RawData>();
        if (raw) {
            for (auto& [apv_id, block] : raw->data) {
                m_raw_data << evt << "," << apv_id << ",";
                std::string sep;
                for (double v : block.all_samples) { m_raw_data << sep << d2s(v); sep = " "; }
                m_raw_data << "\n";
            }
        }

        auto ped = event->GetSingle<ml4fpga::gem::Pedestal>();
        if (ped) {
            for (auto& [apv_id, offsets] : ped->offsets) {
                m_pedestal << evt << "," << apv_id << ",";
                std::string sep;
                for (double v : offsets) { m_pedestal << sep << d2s(v); sep = " "; }
                m_pedestal << ",";
                sep = "";
                for (double v : ped->noises.at(apv_id)) { m_pedestal << sep << d2s(v); sep = " "; }
                m_pedestal << "\n";
            }
        }

        auto apv_dec = event->GetSingle<ml4fpga::gem::ApvDecodedData>();
        if (apv_dec) {
            for (auto& [apv_id, d] : apv_dec->apv_data) {
                for (size_t t = 0; t < d.data.size(); t++) {
                    m_apv_decoded << evt << "," << apv_id << "," << t << ","
                                  << d2s(t < d.CommonModeOffsets.size() ? d.CommonModeOffsets[t] : 0) << ","
                                  << d2s(t < d.RawDataAverage.size() ? d.RawDataAverage[t] : 0) << ",";
                    std::string sep;
                    for (double v : d.data[t]) { m_apv_decoded << sep << d2s(v); sep = " "; }
                    m_apv_decoded << "\n";
                }
            }
        }

        auto plane_dec = event->GetSingle<ml4fpga::gem::PlaneDecodedData>();
        if (plane_dec) {
            for (auto& [plane, d] : plane_dec->plane_data) {
                for (size_t t = 0; t < d.data.size(); t++) {
                    m_plane_decoded << evt << "," << plane << "," << t << ",";
                    std::string sep;
                    for (double v : d.data[t]) { m_plane_decoded << sep << d2s(v); sep = " "; }
                    m_plane_decoded << "\n";
                }
                m_plane_noise << evt << "," << plane << ",";
                std::string sep;
                for (double v : d.PedestalNoises) { m_plane_noise << sep << d2s(v); sep = " "; }
                m_plane_noise << "\n";
            }
        }

        for (auto s : event->Get<ml4fpga::gem::SampleData>()) {
            m_samples << evt << "," << s->id << "," << s->channel << "," << s->raw_channel << ","
                      << s->time_bin << "," << s->apv << "," << s->plane << "," << s->detector << ","
                      << (s->is_noise ? 1 : 0) << "," << d2s(s->value) << "," << d2s(s->raw_value)
                      << "," << d2s(s->rolling_average) << "," << d2s(s->rolling_std) << "\n";
        }
    } catch (std::exception& e) {
        logger()->warn("evt_seq {}: checkpoint dump incomplete: {}", evt, e.what());
    }
}

void FixtureDumpProcessor::Finish() {
    for (auto* f : {&m_srs_raw, &m_raw_data, &m_pedestal, &m_apv_decoded, &m_plane_decoded,
                    &m_plane_noise, &m_peaks, &m_clusters, &m_samples}) {
        f->close();
    }
    logger()->info("Fixture dump finished: {} events", std::min(m_evt_seq, m_nevents));
}
