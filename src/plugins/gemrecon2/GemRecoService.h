// gemrecon2: JANA adapter service owning the gemreco library state (MT plan W2-F).
//
// Three-tier model:
//  - Config: immutable, loaded once from the legacy mapping .cfg (native parser).
//  - Calibration: PedestalBuilder + SampleAnalyzer learn during warm-up (mutex-
//    serialized inside the library), then freeze after gemrecon2:freeze_after
//    events (0 = never freeze -> full legacy-equivalent sequential math).
//  - Workspace: one per worker thread (thread_local in the factory).
#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include <JANA/JApplication.h>
#include <JANA/Services/JServiceLocator.h>

#include <gemreco/calibration.hpp>
#include <gemreco/config.hpp>
#include <gemreco/reconstruct.hpp>
#include <gemreco/steps/decode_apv.hpp>
#include <services/log/Log_service.h>

class GemRecoService : public JService {
public:
    explicit GemRecoService(JApplication* app) : m_app(app) {}

    void acquire_services(JServiceLocator* /*locator*/) override {
        m_log = m_app->GetService<Log_service>()->logger("gem");

        std::string mapping_file = "mapping.cfg";
        m_app->SetDefaultParameter("gemrecon:mapping", mapping_file, "Full path to gem config");
        m_app->SetDefaultParameter("gemrecon:plane_name_x", m_plane_x, "X Plane name (like URWELLX)");
        m_app->SetDefaultParameter("gemrecon:plane_name_y", m_plane_y, "Y Plane name (like URWELLY)");
        m_app->SetDefaultParameter("gemrecon2:freeze_after", m_freeze_after,
                                   "Freeze calibration after N events (0 = never, legacy-equivalent)");

        m_cfg = std::make_shared<const gemreco::Config>(
            gemreco::load_config_mapping_cfg(mapping_file));
        m_ped_builder = std::make_unique<gemreco::PedestalBuilder>(*m_cfg);
        m_sample_analyzer = std::make_unique<gemreco::SampleAnalyzer>(*m_cfg);

        m_log->info("gemreco config loaded: {} APVs, {} planes; freeze_after={}",
                    m_cfg->apvs.size(), m_cfg->planes.size(), m_freeze_after);
    }

    /// Reconstruct one event. Warm-up phase runs the legacy-equivalent sequential
    /// math (calibration learning, serialized inside the builders); after freeze
    /// it is the pure, lock-free gemreco::reconstruct().
    gemreco::RecoResult Reconstruct(const gemreco::RawEvent& event, gemreco::Workspace& ws) {
        if (m_frozen.load(std::memory_order_acquire)) {
            return gemreco::reconstruct(event, *m_cfg, m_calib, ws, m_plane_x, m_plane_y);
        }

        // Learning phase (legacy-equivalent math, order-dependent by design)
        ws.reset();
        auto grouped = gemreco::group_by_apv(event, *m_cfg);
        auto peds = m_ped_builder->process_event(grouped);

        gemreco::RecoResult r;
        for (const auto& [apv_id, block] : grouped) {
            auto it = peds.apvs.find(apv_id);
            if (it == peds.apvs.end()) continue;
            auto d = gemreco::decode_apv_adc(block, it->second, m_cfg->params);
            if (d.data.empty()) continue;
            ws.decoded[apv_id] = std::move(d);
        }
        r.planes = gemreco::merge_planes(ws.decoded, peds, *m_cfg);
        r.peaks = gemreco::find_plane_peaks(r.planes, *m_cfg);
        r.clusters = gemreco::match_clusters(r.planes, *m_cfg, m_plane_x, m_plane_y);
        r.samples = m_sample_analyzer->process_event(event);

        if (m_freeze_after > 0 && m_ped_builder->events_seen() >= m_freeze_after) {
            std::call_once(m_freeze_once, [this]() {
                m_calib.pedestals = m_ped_builder->freeze();
                m_calib.sample_baselines = m_sample_analyzer->freeze();
                m_frozen.store(true, std::memory_order_release);
                m_log->info("Calibration frozen after {} events "
                            "({} APV pedestals, {} sample baselines)",
                            m_ped_builder->events_seen(), m_calib.pedestals.apvs.size(),
                            m_calib.sample_baselines.avg_std.size());
            });
        }
        return r;
    }

    const gemreco::Config& Cfg() const { return *m_cfg; }

private:
    JApplication* m_app = nullptr;
    std::shared_ptr<spdlog::logger> m_log;

    std::shared_ptr<const gemreco::Config> m_cfg;
    std::unique_ptr<gemreco::PedestalBuilder> m_ped_builder;
    std::unique_ptr<gemreco::SampleAnalyzer> m_sample_analyzer;

    gemreco::FrozenCalibration m_calib;
    std::atomic<bool> m_frozen{false};
    std::once_flag m_freeze_once;

    std::string m_plane_x = "URWELLX";
    std::string m_plane_y = "URWELLY";
    size_t m_freeze_after = 500;
};
