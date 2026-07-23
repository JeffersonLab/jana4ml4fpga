#include "FpgaDqmProcessor.h"

#include <JANA/JApplication.h>
#include <JANA/JEvent.h>

#include <Math/GenVector/PxPyPzM4D.h>

#include <spdlog/spdlog.h>
#include <services/root_output/RootFile_service.h>

#include <TSocket.h>
#include <TMarker.h>
#include <TCanvas.h>
#include <TH2.h>
#include <TF1.h>
#include <TROOT.h>
#include <rawdataparser/Df125WindowRawData.h>

#include "F125Cluster.h"
#include "F125ClusterContext.h"
#include "services/dqm/DataQualityMonitorService.h"
#include "FpgaHitsToTrack.h"
#include "FpgaTrackFit.h"
#include "FpgaExchangeTimeInfo.h"


//------------------
// Init
//------------------
void FpgaDqmProcessor::Init() {
    std::string plugin_name = GetPluginName();

    // Get JANA application
    auto app = GetApplication();

    // Get Log level from user parameter or default
    InitLogger(plugin_name + "_proc");

    // Ask service locator a file to write histograms to
    m_dqm_service = app->GetService<DataQualityMonitorService>();

    logger()->info("This plugin name is: " + GetPluginName());
    logger()->info(JTypeInfo::demangle<FpgaDqmProcessor>() + " initialization is done");
}


//------------------
// Process
//------------------
// This function is called every event
void FpgaDqmProcessor::Process(const std::shared_ptr<const JEvent>&event) {
    try {
        // We use this m_total_event_num because when there are several files of the same accelerator-run
        // we have the same event numbers and have memory leaks with histograms having the same names
        m_total_event_num++;

        if (event->GetEventNumber() < 3) return;

        try {
            bool should_fill_event = m_dqm_service->ShouldProcessEvent(event->GetEventNumber());

            if (!should_fill_event) {
                return;
            }
        }
        catch (std::exception&exp) {
            m_log->error("Error during process");
            m_log->error("Exception what()='{}', type='{}'", exp.what(), typeid(exp).name());
            throw new JException(exp.what());
        }


        try {
            auto time_info = event->GetSingle<ml4fpga::fpgacon::FpgaExchangeTimeInfo>();

            ProcessTimeInfo(time_info);
        }
        catch (std::exception&exp) {
            m_log->error("Error during time_info");
            m_log->error("Exception what()='{}', type='{}'", exp.what(), typeid(exp).name());
            throw new JException(exp.what());
        }




        auto context = event->GetSingle<ml4fpga::fpgacon::F125ClusterContext>();

        auto hists_dir = m_dqm_service->GetPerEventSubDir(m_total_event_num, "fpgacon");
        hists_dir->cd();

        auto hevt = static_cast<TH2 *>(context->hevt->Clone("hevt-n"));
        hevt->SetDirectory(hists_dir);

        auto hevtc = static_cast<TH2 *>(context->hevtc->Clone("hevtc-n"));
        hevtc->SetDirectory(hists_dir);


        // Fill peaks on the instagram
        gROOT->SetBatch(kTRUE);
        uint64_t event_number = m_total_event_num;
        //-----------------  canvas 1 FPGA Display ----------
        std::string title = fmt::format("Clustering event {}", m_total_event_num);
        TCanvas* clust_canvas = new TCanvas("clusters", title.c_str(), 100, 100, 1000, 1300);
        clust_canvas->cd();
        hevt->Draw("colz");
        int COLMAP[] = {1, 2, 3, 4, 6, 5};

        logger()->trace("{:>10} {:>10} {:>10} {:>10}", "id", "pos_x", "pos_z", "dedx");
        auto clusters = event->Get<ml4fpga::fpgacon::F125Cluster>();
        for (auto&cluster: clusters) {
            logger()->trace("{:>10} {:>10.2f} {:>10.2f} {:>10.2f}", cluster->id, cluster->pos_x, cluster->pos_z, cluster->dedx);

            // Put marker on the prlot
            int mstyle = cluster->size < m_cfg_min_clust_size ? 22 : 20; // Cluster marker style
            TMarker m = TMarker(cluster->pos_z, cluster->pos_x, mstyle);

            // Cluster marker color
            int tcol = 2;
            int mcolor = COLMAP[tcol - 1];
            m.SetMarkerColor(mcolor);
            m.SetMarkerSize(0.7 + cluster->dedx / 300); // Parametrize?
            m.DrawClone("same");
            clust_canvas->Modified();
            clust_canvas->Update();
        }
        //canvas->Modified();
        clust_canvas->Update();
        clust_canvas->Write();
        delete clust_canvas;


        // Fit canvas

        title = fmt::format("Fpga matching and fitting event {}", m_total_event_num);
        auto tfit_canvas = new TCanvas("tracks", title.c_str(), 100, 100, 1000, 1300);
        tfit_canvas->cd();
        hevt->Draw("colz");

        double zStart = 5.; // mm
        double zEnd = 29.; // mm


        logger()->trace("{:>10} {:>10} {:>10} {:>10}", "id", "pos_x", "pos_z", "dedx");
        auto hit_track_assocs = event->Get<ml4fpga::fpgacon::FpgaHitsToTrack>();
        auto track_fits = event->Get<ml4fpga::fpgacon::FpgaTrackFit>();
        for (auto tfit: track_fits) {
            TF1 ftrk("ftrk", "[0]*x+[1]", zStart, zEnd);
            ftrk.SetParameter(0, tfit->slope);
            ftrk.SetParameter(1, tfit->intersect);
            ftrk.DrawClone("same");
        }

        for (int i = 0; i < hit_track_assocs.size(); i++) {
            auto hit_track_assoc = hit_track_assocs[i];

            // track_index == 0 means this hit has no association
            if (hit_track_assoc->track_index == 0 || track_fits.empty()) {
                continue;
            }

            // Check if result is in range of cluster indexes
            if (hit_track_assoc->track_index < 0 || hit_track_assoc->track_index - 1 >= track_fits.size()) {
                logger()->warn("hit_track_assoc->track_index={} is outside of tracks indexes track_fits.size()={}", hit_track_assoc->track_index, track_fits.size());
                continue;
            }

            // Check if result is in range of cluster indexes
            if (hit_track_assoc->hit_index < 0 || hit_track_assoc->hit_index >= clusters.size()) {
                logger()->warn("hit_track_assoc->hit_index={} is outside of cluster indexes clusters.size()={}", hit_track_assoc->hit_index, clusters.size());
                continue;
            }

            auto&cluster = clusters[hit_track_assoc->hit_index];
            logger()->trace("{:>10} {:>10.2f} {:>10.2f} {:>10.2f}", cluster->id, cluster->pos_x, cluster->pos_z, cluster->dedx);

            // Put marker on the prlot
            int mstyle = cluster->size < m_cfg_min_clust_size ? 22 : 20; // Cluster marker style
            TMarker m = TMarker(cluster->pos_z, cluster->pos_x, mstyle);

            // Cluster marker color
            int tcol = std::min(hit_track_assoc->track_index, 6);
            int mcolor = COLMAP[tcol - 1];
            m.SetMarkerColor(mcolor);

            // Size
            m.SetMarkerSize(0.7 + cluster->dedx / 300); // Parametrize?

            // Update everything
            m.DrawClone("same");
            tfit_canvas->Modified();
            tfit_canvas->Update();
        }

        //canvas->Modified();
        tfit_canvas->Update();
        tfit_canvas->Write();
        delete tfit_canvas;
    }
    catch (std::exception&e) {
        logger()->warn("FpgaDqmProcessor Exception during process {}", e.what());
        //m_logger->warn("Exception during process {}", e.what());
        //throw JException();
    }
}


//------------------
// Finish
//------------------
void FpgaDqmProcessor::Finish() {
    //    m_log->trace("FpgaConnectionProcessor finished\n");
    // if(m_h1d_timing_event_cpu_time) {
    // m_h1d_timing_event_cpu_time->Write();
    // delete m_h1d_timing_event_cpu_time;
    // }
    // if(m_h1d_timing_event_real_time) {
    // m_h1d_timing_event_real_time->Write();
    // delete m_h1d_timing_event_real_time;
    // }
    // if(m_h1d_timing_send_cpu_time) {
    // m_h1d_timing_send_cpu_time->Write();
    // delete m_h1d_timing_send_cpu_time;
    // }
    // if(m_h1d_timing_send_real_time) {
    // m_h1d_timing_send_real_time->Write();
    // delete m_h1d_timing_send_real_time;
    // }
    // if(m_h1d_timing_receive1_cpu_time) {
    // m_h1d_timing_receive1_cpu_time->Write();
    // delete m_h1d_timing_receive1_cpu_time;
    // }
    // if(m_h1d_timing_receive1_real_time) {
    // m_h1d_timing_receive1_real_time->Write();
    // delete m_h1d_timing_receive1_real_time;
    // }
    // if(m_h1d_timing_receive2_cpu_time) {
    // m_h1d_timing_receive2_cpu_time->Write();
    // delete m_h1d_timing_receive2_cpu_time;
    // }
    // if(m_h1d_timing_receive2_real_time) {
    // m_h1d_timing_receive2_real_time->Write();
    // delete m_h1d_timing_receive2_real_time;
    // }
}

void FpgaDqmProcessor::ProcessTimeInfo(const ml4fpga::fpgacon::FpgaExchangeTimeInfo* time_info)
{

    if(m_h1d_timing_event_cpu_time == nullptr)
    {
        auto integ_dir = m_dqm_service->GetIntegralSubDir("fpgacon");
        integ_dir->cd();
        m_h1d_timing_event_cpu_time     = new TH1F("timing_event_cpu_time", "Time information m_h1d_timing_event_cpu_time    ", 100, -0.5, 999.5);
        m_h1d_timing_event_real_time    = new TH1F("timing_event_real_time", "Time information m_h1d_timing_event_real_time   ", 100, -0.5, 999.5);
        m_h1d_timing_send_cpu_time      = new TH1F("timing_send_cpu_time", "Time information m_h1d_timing_send_cpu_time     ", 100, -0.5, 999.5);
        m_h1d_timing_send_real_time     = new TH1F("timing_send_real_time", "Time information m_h1d_timing_send_real_time    ", 100, -0.5, 999.5);
        m_h1d_timing_receive1_cpu_time  = new TH1F("timing_receive1_cpu_time", "Time information m_h1d_timing_receive1_cpu_time ", 100, -0.5, 999.5);
        m_h1d_timing_receive1_real_time = new TH1F("timing_receive1_real_time", "Time information m_h1d_timing_receive1_real_time", 100, -0.5, 999.5);
        m_h1d_timing_receive2_cpu_time  = new TH1F("timing_receive2_cpu_time", "Time information m_h1d_timing_receive2_cpu_time ", 100, -0.5, 999.5);
        m_h1d_timing_receive2_real_time = new TH1F("timing_receive2_real_time", "Time information m_h1d_timing_receive2_real_time", 100, -0.5, 999.5);

        m_h1d_timing_event_cpu_time->SetDirectory(integ_dir);
        m_h1d_timing_event_real_time->SetDirectory(integ_dir);
        m_h1d_timing_send_cpu_time->SetDirectory(integ_dir);
        m_h1d_timing_send_real_time->SetDirectory(integ_dir);
        m_h1d_timing_receive1_cpu_time->SetDirectory(integ_dir);
        m_h1d_timing_receive1_real_time->SetDirectory(integ_dir);
        m_h1d_timing_receive2_cpu_time->SetDirectory(integ_dir);
        m_h1d_timing_receive2_real_time->SetDirectory(integ_dir);
    }

    m_h1d_timing_event_cpu_time->Fill(time_info->event_cpu_time * 1000000    );
    m_h1d_timing_event_real_time->Fill(time_info->event_real_time * 1000000   );
    m_h1d_timing_send_cpu_time->Fill(time_info->send_cpu_time * 1000000     );
    m_h1d_timing_send_real_time->Fill(time_info->send_real_time * 1000000    );
    m_h1d_timing_receive1_cpu_time->Fill(time_info->receive1_cpu_time * 1000000 );
    m_h1d_timing_receive1_real_time->Fill(time_info->receive1_real_time * 1000000 );
    m_h1d_timing_receive2_cpu_time->Fill(time_info->receive2_cpu_time * 1000000 );
    m_h1d_timing_receive2_real_time->Fill(time_info->receive2_real_time * 1000000 );
}
