#pragma once

#include <JANA/JEventProcessor.h>
#include <JANA/JEventProcessorSequentialRoot.h>

#include <TDirectory.h>
#include <TH1F.h>
#include <TH2F.h>

#include <extensions/spdlog/SpdlogMixin.h>
#include <rawdataparser/DGEMSRSWindowRawData.h>
#include <services/dqm/DataQualityMonitorService.h>

#include "DecodedData.h"
#include "SFclust.h"
#include "PlanePeak.h"

class GemMapping;
class JEvent;
class JApplication;


namespace ml4fpga::gem {
    class GemReconDqmProcessor :
            public JEventProcessor,
            public spdlog::extensions::SpdlogMixin<GemReconDqmProcessor> // this automates proper Log initialization
    {
    public:
        explicit GemReconDqmProcessor(JApplication *);

        ~GemReconDqmProcessor() override = default;

        //----------------------------
        // Init
        //
        // This is called once before the first call to the Process method
        // below. You may, for example, want to open an output file here.
        // Only one thread will call this.
        void Init() override;


        void FillIntegralTimePeakData(const std::shared_ptr<const JEvent> &event);

        void FillSampleData(const std::shared_ptr<const JEvent> &event);

        //----------------------------
        // Process
        //
        // This is called for every event. Multiple threads may call this
        // simultaneously. If you write something to an output file here
        // then make sure to protect it with a mutex or similar mechanism.
        // Minimize what is done while locked since that directly affects
        // the multi-threaded performance.
        void Process(const std::shared_ptr<const JEvent> &event) override;

        //----------------------------
        // Finish
        //
        // This is called once after all events have been processed. You may,
        // for example, want to close an output file here.
        // Only one thread will call this.
        void Finish() override;

    private:
        TH1F *m_h1d_gem_prerecon_x = nullptr;
        TH1F *m_h1d_gem_prerecon_y = nullptr;

        size_t m_events_count = 0;

        GemMapping *m_mapping;

        std::shared_ptr<DataQualityMonitorService> m_dqm_service;
        std::string m_name_plane_x = "URWELLX";
        std::string m_name_plane_y = "URWELLY";
        std::map<int, std::string> m_apv_id_names_map;

        void FillEventRawData(const std::shared_ptr<const JEvent> &event);

        void FillApvDecodedData(const std::shared_ptr<const JEvent> &event);

        void FillPreReconData(const std::shared_ptr<const JEvent> &event);

        void FillEventPlaneData(const std::shared_ptr<const JEvent> &event);

        void FillIntegralPlaneData(const std::shared_ptr<const JEvent> &event);


        void FillEventClusters(const std::shared_ptr<const JEvent> &event);

        void FillIntegralClusters(const std::shared_ptr<const JEvent> &event);

        void FillEventPeaks(const std::shared_ptr<const JEvent> &event);

        void FillIntegralPeaks(const std::shared_ptr<const JEvent> &event);

        TH1F *m_h1d_cluster_count = nullptr;
        TH1F *m_h1d_cluster_pos_x = nullptr;
        TH1F *m_h1d_cluster_pos_y = nullptr;
        TH2F *m_h2d_cluster_pos_xy = nullptr;
        TH1F *m_h1d_cluster_idx_x = nullptr;
        TH1F *m_h1d_cluster_idx_y = nullptr;
        TH2F *m_h2d_cluster_idx_xy = nullptr;
        TH1F *m_h2d_cluster_amp = nullptr;
        TH1F *m_h2d_cluster_energy = nullptr;

        std::map<std::string, TH1F *> m_planes_h1d_data;
        std::map<std::string, TH2F *> m_planes_h2d_data;

        // We use this m_total_event_num because when there are several files of the same accelerator-run
        // we have the same event numbers and have memory leaks with histograms having the same names
        uint64_t m_total_event_num = 0;
    };
} // namespace ml4fpga::gem

