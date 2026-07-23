#pragma once

#include <JANA/JEventProcessor.h>
#include <JANA/JEventProcessorSequentialRoot.h>
#include <extensions/spdlog/SpdlogMixin.h>
#include <TDirectory.h>

#include <TSocket.h>
#include <TMarker.h>
#include <TCanvas.h>
#include <TH2.h>
#include <TF1.h>
#include <rawdataparser/Df125WindowRawData.h>

#include "FpgaExchangeTimeInfo.h"
#include "services/dqm/DataQualityMonitorService.h"
#include "FpgaExchangeTimeInfo.h"

class JEvent;
class JApplication;

class FpgaDqmProcessor:
        public JEventProcessor,
        public spdlog::extensions::SpdlogMixin<FpgaDqmProcessor>   // this automates proper Log initialization
{
public:
    explicit FpgaDqmProcessor(JApplication *app): JEventProcessor(app) {};
    ~FpgaDqmProcessor() override = default;

    //----------------------------
    // Init
    //
    // This is called once before the first call to the Process method
    // below. You may, for example, want to open an output file here.
    // Only one thread will call this.
    void Init() override;

    //----------------------------
    // Process
    //
    // This is called for every event. Multiple threads may call this
    // simultaneously. If you write something to an output file here
    // then make sure to protect it with a mutex or similar mechanism.
    // Minimize what is done while locked since that directly affects
    // the multi-threaded performance.
    void Process(const std::shared_ptr<const JEvent>& event) override;

    //----------------------------
    // Finish
    //
    // This is called once after all events have been processed. You may,
    // for example, want to close an output file here.
    // Only one thread will call this.
    void Finish() override;




private:

    /// Directory to store histograms to
    TDirectory *m_dir_main{};
    std::shared_ptr<DataQualityMonitorService> m_dqm_service;

    float m_cfg_min_clust_size=10;

    // We use this m_total_event_num because when there are several files of the same accelerator-run
    // we have the same event numbers and have memory leaks with histograms having the same names
    uint64_t m_total_event_num = 0;

    void ProcessTimeInfo(const ml4fpga::fpgacon::FpgaExchangeTimeInfo* time_info);

    TH1F *m_h1d_timing_event_cpu_time     = nullptr;
    TH1F *m_h1d_timing_event_real_time    = nullptr;
    TH1F *m_h1d_timing_send_cpu_time      = nullptr;
    TH1F *m_h1d_timing_send_real_time     = nullptr;
    TH1F *m_h1d_timing_receive1_cpu_time  = nullptr;
    TH1F *m_h1d_timing_receive1_real_time = nullptr;
    TH1F *m_h1d_timing_receive2_cpu_time  = nullptr;
    TH1F *m_h1d_timing_receive2_real_time = nullptr;
};

