#include <filesystem>

#include <spdlog/spdlog.h>

#include <JANA/JApplication.h>
#include <JANA/JEvent.h>

#include <Math/GenVector/PxPyPzM4D.h>

#include <rawdataparser/DGEMSRSWindowRawData.h>
#include <rawdataparser/Df125WindowRawData.h>
#include <rawdataparser/Df125FDCPulse.h>
#include <extensions/root/DirectorySwitcher.h>
#include <services/dqm/DataQualityMonitorService.h>
#include <TROOT.h>
#include <TCanvas.h>
#include <TBox.h>
#include <TPaveText.h>
#include <TLine.h>
#include <TPaveStats.h>
#include <TLegend.h>

#include "RawData.h"
#include "Pedestal.h"
#include "GemReconDqmProcessor.h"

#include <TCanvas.h>

#include "ApvDecodedDataFactory.h"
#include "ClusterFactory.h"
#include "GemMappingService.h"
#include "SampleData.h"


//-------------------------------------
// GemReconDqmProcessor (Constructor)
//-------------------------------------
ml4fpga::gem::GemReconDqmProcessor::GemReconDqmProcessor(JApplication *app): JEventProcessor(app)
{
    m_events_count = 0;
}

//-------------------------------------
// Init
//-------------------------------------
void ml4fpga::gem::GemReconDqmProcessor::Init() {
    std::string plugin_name = GetPluginName();

    // Get JANA application
    auto app = GetApplication();

    // Ask service locator a file to write histograms to
    m_dqm_service = app->GetService<DataQualityMonitorService>();

    // Spawn gem reco directory
    auto dir_gem_reco = m_dqm_service->GetIntegralSubDir("gem_reco");

    // Get Log level from user parameter or default
    InitLogger(plugin_name + ":" + JTypeInfo::demangle<GemReconDqmProcessor>());

    // Get mapping
    m_mapping = app->GetService<GemMappingService>()->GetMapping();
    m_apv_id_names_map = m_mapping->GetAPVFromIDMap();

    // Plane names to monitor. Same parameter keys as ClusterFactory so one
    // -Pgemrecon:plane_name_x/_y configures the whole plugin consistently.
    app->SetDefaultParameter("gemrecon:plane_name_x", m_name_plane_x, "X Plane name (like URWELLX)");
    app->SetDefaultParameter("gemrecon:plane_name_y", m_name_plane_y, "Y Plane name (like URWELLY)");

    // Create histograms
    int nbins = 2 * Constants::ChannelsCount;
    double plane_size_x = m_mapping->GetPlaneSize(m_name_plane_x);
    double plane_size_y = m_mapping->GetPlaneSize(m_name_plane_y);
    m_h1d_cluster_count = new TH1F("gem_clust_count", "Number of clusters in event", 10, -0.5, 9.5);
    m_h1d_cluster_pos_x = new TH1F("gem_clust_pos_x", "Reconstructed clusters for plane X", 100, -plane_size_x, plane_size_x);
    m_h1d_cluster_pos_y = new TH1F("gem_clust_pos_y", "Reconstructed clusters for plane Y", 100, -plane_size_y, plane_size_y);
    m_h2d_cluster_pos_xy = new TH2F("gem_clust_pos_xy", "Reconstructed clusters XY", 128, -plane_size_x, plane_size_x, 128, -plane_size_y, plane_size_y);
    m_h1d_cluster_idx_x = new TH1F("gem_clust_idx_x", "Reconstructed clusters for plane X", nbins, -0.5, nbins - 0.5);
    m_h1d_cluster_idx_y = new TH1F("gem_clust_idx_y", "Reconstructed clusters for plane Y", nbins, -0.5, nbins - 0.5);
    m_h2d_cluster_idx_xy = new TH2F("gem_cluster_idx_xy", "Reconstructed clusters XY indexes", nbins, -0.5, nbins - 0.5, nbins, -0.5, nbins - 0.5);
    m_h2d_cluster_energy = new TH1F("gem_cluster_energy", "Reconstructed clusters Amplitude", 100, 0, 100);
    m_h2d_cluster_amp = new TH1F("gem_cluster_amp", "Reconstructed clusters Amplitude", 100, 0, 3000);

    //  D O N E
    logger()->info("Initialization done");
}



//------------------
// Process
//------------------
// This function is called every event
void ml4fpga::gem::GemReconDqmProcessor::Process(const std::shared_ptr<const JEvent> &event) {
    m_log->debug(" new event");
    try {
        // We use this m_total_event_num because when there are several files of the same accelerator-run
        // we have the same event numbers and have memory leaks with histograms having the same names
        m_total_event_num++;

        // Get mapping
        m_mapping = GemMapping::GetInstance();

        bool should_fill_event = m_dqm_service->ShouldProcessEvent(m_total_event_num);

        if(!should_fill_event) {
            return;
        }

        // Raw GEM data
        FillEventRawData(event);

        // Fill APV data
        FillApvDecodedData(event);


        // Data decoded for a plane
        FillEventPlaneData(event);
        FillIntegralPlaneData(event);

        // Clusters
        FillEventClusters(event);
        FillIntegralClusters(event);

        // Peaks
        FillEventPeaks(event);
        FillIntegralPeaks(event);

        // TimeBinData
        FillIntegralTimePeakData(event);

        // TimeBinData
        FillSampleData(event);
    }
    catch (std::exception &exp) {
        std::string no_factory_message = "Could not find JFactoryT<DGEMSRSWindowRawData>";
        if (std::string(exp.what()).find(no_factory_message) != std::string::npos) {
            // The first events might be some technical data and maybe DGEMSRSWindowRawData is not yet there so
            // we skip this. For later events we make it a warning
            std::string log_message = fmt::format("{} at event={} total_event_num={}",
                                                  no_factory_message, event->GetEventNumber(), m_total_event_num);
            if(event->GetEventNumber() < 10) {
                m_log->debug(log_message);
            } else {
                m_log->warn(log_message);
            }
        }
        m_log->error("Error during process");
        m_log->error("Exception what()='{}', type='{}'", exp.what(), typeid(exp).name());
    }
}


//------------------
// Finish
//------------------
void ml4fpga::gem::GemReconDqmProcessor::Finish() {
        m_log->trace("Finished\n");
}


void ml4fpga::gem::GemReconDqmProcessor::FillEventRawData(const std::shared_ptr<const JEvent> &event) {


    // Get data
    auto srs_data = event->Get<DGEMSRSWindowRawData>();
    auto hists_dir = m_dqm_service->GetPerEventSubDir(m_total_event_num, "gem_raw");
    uint64_t event_number = m_total_event_num;

    // check srs data is not empty and there are samples
    if(srs_data.size() == 0 || srs_data[0]->samples.size() == 0) {
        return;
    }

    // get sample size and number of bins
    int samples_size = srs_data[0]->samples.size();
    int nbins = samples_size * 128 + (samples_size-1);

    // Sometimes there is no last sample. We NEED it to be coherent. Check, warn and skip if there are problems
    for(auto srs_item: srs_data) {
        if (samples_size != srs_item->samples.size()) {
            m_log->warn("samples_size_0 = {} != srs_item->samples.size() = {} at event# = {} m_total_event_num={}", samples_size,  srs_item->samples.size(), event->GetEventNumber(), m_total_event_num);
            return;
        }
    }

    // This is going to be map<apvId, map<channelNum, vector<samples>>>
    std::map<int, std::map<int, std::vector<int> > > event_map;

    // Making a map
    for(auto srs_item: srs_data) {

        // Dumb copy of samples because one is int and the other is uint16_t
        std::vector<int> samples;
        for(size_t i=0; i< srs_item->samples.size(); i++) {
            samples.push_back(srs_item->samples[i]);
        }

        event_map[(int)srs_item->apv_id][(int)srs_item->channel_apv] = samples;
    }

    auto saved_gdir = gDirectory;  // save current directory
    hists_dir->cd();

    // Now lets go over this map
    for(auto apvPair: event_map) {
        auto apv_id = apvPair.first;
        auto apv_channels = apvPair.second;

        // It might be EVIO contains APV that we don't need to process
        if(!m_apv_id_names_map.count(apv_id)) continue;


        // Crate histogram
        std::string apv_name = m_apv_id_names_map[apv_id];
        std::string histo_name = fmt::format("raw_{}", apv_name);
        std::string histo_title = fmt::format("SRSRawWindowSata for Event# {} APV {}-{}", event_number, apv_id, apv_name);

        auto histo= new TH1F(histo_name.c_str(), histo_title.c_str(), nbins, -0.5, nbins - 0.5);
        histo->SetDirectory(hists_dir);

        // go over channels
        for(auto channel_pair: apv_channels) {
            auto apv_channel = channel_pair.first;
            auto samples = channel_pair.second;
            // go over samples
            for(size_t sample_i=0; sample_i < samples.size(); sample_i++){
                int bin_index = sample_i*128 + sample_i + m_mapping->APVchannelCorrection(apv_channel);  // + sample_i to make gaps in between samples
                histo->SetBinContent(bin_index, samples[sample_i]);
            }
        }
    }

    // Restore current directory
    if(saved_gdir) saved_gdir->cd();
}

void ml4fpga::gem::GemReconDqmProcessor::FillApvDecodedData(const std::shared_ptr<const JEvent> &event) {


    auto  hists_dir = m_dqm_service->GetPerEventSubDir(m_total_event_num, "gem_apv");
    uint64_t event_number = m_total_event_num;

    // Data decoded for each APV
    auto data = event->GetSingle<ApvDecodedData>();
    if(!data) {
        m_log->warn("No ApvDecodedData for GemReconDqmProcessor");
        return;
    }

    auto saved_gdir = gDirectory;  // save current directory
    hists_dir->cd();

    // Now lets go over this
    for(auto apv_pair: data->apv_data) {
        auto apv_id = apv_pair.first;
        auto &adc_data = apv_pair.second;
        auto &timebins = adc_data.data;

        // Have data for this APV?
        if(timebins.size() == 0) {
            return;
        }

        size_t nbins = timebins.size() * 128 + (timebins.size()-1);
        auto apv_name = fmt::format("apv_{}_{}", apv_id,  m_mapping->GetPlaneFromAPVID(apv_id));

        // Crate DATA histogram
        std::string histo_name = fmt::format("{}_data", apv_name);
        std::string histo_title = fmt::format("SRS Raw Window data for Event# {} APV {}", event_number, apv_name);
        auto data_hist= new TH1F(histo_name.c_str(), histo_title.c_str(), nbins, -0.5, nbins - 0.5);
        data_hist->SetDirectory(hists_dir);

        // go over channels
        for(size_t time_i = 0; time_i < timebins.size(); time_i++) {
            for(size_t ch_i = 0; ch_i < Constants::ChannelsCount; ch_i++)
            {
                // go over samples
                int bin_index = time_i*128 + time_i + ch_i;  // + time_i to make gaps in between samples
                data_hist->SetBinContent(bin_index, timebins[time_i][ch_i]);
            }
        }

        // HISTOGRAM Common modes offsets
        histo_name = fmt::format("{}_common_mode", apv_name);
        histo_title = fmt::format("CommonModeOffsets for Event# {} APV {} {}", event_number, apv_id, apv_name);
        auto cm_count = adc_data.CommonModeOffsets.size();
        auto cm_hist = new TH1F(histo_name.c_str(), histo_title.c_str(), cm_count, -0.5, cm_count - 0.5);
        cm_hist->SetDirectory(hists_dir);
        for(size_t i=0; i < adc_data.CommonModeOffsets.size(); i++) {
            cm_hist->SetBinContent(i, adc_data.CommonModeOffsets[i]);
        }

        // HISTOGRAM Pedestals
        histo_name = fmt::format("{}_pedestals", apv_name);
        histo_title = fmt::format("Pedestals for Event# {} APV {} {}", event_number, apv_id, apv_name);
        auto pedestals_count = adc_data.PedestalOffsets.size();
        assert(pedestals_count == 128);
        auto pd_hist = new TH1F(histo_name.c_str(), histo_title.c_str(), pedestals_count, -0.5, pedestals_count - 0.5);
        pd_hist->SetDirectory(hists_dir);
        for(size_t i=0; i < pedestals_count; i++) {
            pd_hist->SetBinContent(i, adc_data.PedestalOffsets[i]);
        }

        // HISTOGRAM Noises
        histo_name = fmt::format("{}_noises", apv_name);
        histo_title = fmt::format("Noises for Event# {} APV {} {}", event_number, apv_id, apv_name);
        auto noises_count = adc_data.PedestalNoises.size();
        auto noise_hist = new TH1F(histo_name.c_str(), histo_title.c_str(), noises_count, -0.5, noises_count - 0.5);
        noise_hist->SetDirectory(hists_dir);
        for(size_t i=0; i < adc_data.PedestalNoises.size(); i++) {
            noise_hist->SetBinContent(i, adc_data.PedestalNoises[i]);
        }
    }

    if(saved_gdir) saved_gdir->cd();  // restore gDirectory
}


void ml4fpga::gem::GemReconDqmProcessor::FillPreReconData(const std::shared_ptr<const JEvent> &event) {

//     auto data = event->GetSingle<PreReconData>();
//
//
//     // Now lets go over this
//     for(auto smpl_x: data->samples_x) {
//         for(auto smpl_x: data->samples_y) {
//
//
//
// //            }
// //            auto apv_id = apv_pair.first;
// //            auto &timebins = apv_pair.second.data;
// //
// //            size_t nbins = timebins.size() * 128 + (timebins.size()-1);
// //
// //            // Crate histogram
// //            std::string histo_name = fmt::format("evt_{}_xy_{}", event_number, fMapping->GetAPVFromID(apv_id));
// //            std::string histo_title = fmt::format("SRS Raw Window data for Event# {} APV {}", event_number, apv_id);
// //
// //            auto histo= new TH1F(histo_name.c_str(), histo_title.c_str(), nbins, -0.5, nbins - 0.5);
// //            histo->SetDirectory(pDirectory);
// //
// //            // go over channels
// //            for(size_t time_i = 0; time_i < timebins.size(); time_i++) {
// //                for(size_t ch_i = 0; ch_i < Constants::ChannelsCount; ch_i++)
// //                {
// //                    // go over samples
// //                    int bin_index = time_i*128 + time_i + ch_i;  // + time_i to make gaps in between samples
// //                    histo->SetBinContent(bin_index, timebins[time_i][ch_i]);
// //                }
// //            }
//
//                 //histo->Write();
//         }
//     }
}

void ml4fpga::gem::GemReconDqmProcessor::FillEventPlaneData(const std::shared_ptr<const JEvent> &event) {

    /// This function fills 3 types of histograms for each plane:
    /// 1. Plane data for all channels all time bins for this event
    /// 2. Same but for all events
    /// 3. Same as 2 but 2d

    // Get data
    auto data = event->GetSingle<PlaneDecodedData>();
    auto events_dir = m_dqm_service->GetPerEventSubDir(m_total_event_num, "gem_plane");
    auto integral_dir = m_dqm_service->GetIntegralSubDir("gem_plane");
    auto event_number = m_total_event_num;

    std::unordered_map<std::string, TH1F*> event_data_histos_by_plane;
    std::unordered_map<std::string, TH1F*> event_noise_histos_by_plane;
    int time_frame_count = 0;
    int nbins = 0;
    int adc_count = 0;
    //fmt::print("here 0\n");

    // Iterate over planes
    for(const auto& kv: data->plane_data) {
        auto plane_name = kv.first;
        const auto& plane_data = kv.second;
        //fmt::print("here 1 plane_name={}\n", plane_name);

        std::string histo_name = fmt::format("plane_{}", plane_name);
        std::string histo_title = fmt::format("{} data for event {}", plane_name, event_number);
        time_frame_count = plane_data.data.size();
        //fmt::print("time_frame_count={}\n", time_frame_count);
        if(time_frame_count == 0) {
            continue;
        }

        adc_count = plane_data.data[0].size();
        nbins = time_frame_count * adc_count;

        // Initialize for this event histogram
        auto event_data_histo = new TH1F(histo_name.c_str(), histo_title.c_str(), nbins, -0.5, nbins - 0.5);
        auto event_noise_histo = new TH1F(("noise_" + histo_name).c_str(), histo_title.c_str(), nbins, -0.5, nbins - 0.5);
        event_data_histo->SetDirectory(events_dir);
        event_noise_histo->SetDirectory(events_dir);

        //fmt::print("here 1 make histos\n", plane_name);

        // Initialize 1D integral histo
        TH1F* integral_1d_histo;
        histo_title = fmt::format("{} data for processed events", plane_name);
        if(!m_planes_h1d_data.count(histo_name)) {
            integral_1d_histo = new TH1F(histo_name.c_str(), histo_title.c_str(), nbins, -0.5, nbins - 0.5);
            integral_1d_histo->SetDirectory(integral_dir);
            m_planes_h1d_data[histo_name] = integral_1d_histo;
        }
        else
        {
            integral_1d_histo = m_planes_h1d_data[histo_name];
        }

        //fmt::print("here 1 init histos\n", plane_name);

        // Initialize 2D integral histo
        TH2F* integral_2d_histo;
        auto histo2d_name = histo_name+"2d";
        histo_title = fmt::format("{} data over all times", plane_name);
        if(!m_planes_h2d_data.count(histo2d_name)) {
            integral_2d_histo = new TH2F(histo2d_name.c_str(), histo_title.c_str(), 256, -0.5, 256 - 0.5, 175, 0, 2000);
            integral_2d_histo->SetDirectory(integral_dir);
            m_planes_h2d_data[histo2d_name] = integral_2d_histo;
        }
        else
        {
            integral_2d_histo = m_planes_h2d_data[histo2d_name];
        }

        //fmt::print("here 1 init histos2\n", plane_name);

        // Fill the histogram
        for(size_t time_i=0; time_i < time_frame_count; time_i++) {
            for(size_t adc_i=0; adc_i < adc_count; adc_i++) {
                int index = adc_count * time_i + adc_i;
                event_data_histo->SetBinContent(index, plane_data.data[time_i][adc_i]);
                event_noise_histo->SetBinContent(index, plane_data.PedestalNoises[adc_i]);
                integral_1d_histo->AddBinContent(index, plane_data.data[time_i][adc_i]);
                integral_2d_histo->Fill(adc_i, plane_data.data[time_i][adc_i]);
            }

            if(time_i > 0) {
                event_data_histo->SetBinContent(adc_count * time_i, 0);
            }
        }
        //fmt::print("here 1 fill histos\n", plane_name);


        event_data_histos_by_plane[plane_name] = event_data_histo;
        event_noise_histos_by_plane[plane_name] = event_noise_histo;
    }
    //fmt::print("here PEAKS\n");

    // Fill peaks on the instagram
    gROOT->SetBatch(kTRUE);

    auto peaks = event->Get<PlanePeak>();
    std::unordered_map<std::string, TCanvas*> peak_canvases_by_plane;
    std::unordered_map<std::string, TLegend*> peak_paves_by_plane;
    for(auto &peak: peaks) {

        TCanvas *canvas = nullptr;
        TLegend *legend = nullptr;
        auto data_hist = event_data_histos_by_plane[peak->plane_name];
        auto noise_hist = event_noise_histos_by_plane[peak->plane_name];

        if(!peak_canvases_by_plane.count(peak->plane_name)) {
            events_dir->cd();

            // Create canvas
            auto cnv_name = fmt::format("{}_peaks", peak->plane_name);
            canvas = new TCanvas(cnv_name.c_str(),"Histogram Drawing Options",200,10,700,900);
            peak_canvases_by_plane[peak->plane_name] = canvas;

            // Draw a histogram
            data_hist->Draw();
            //noise_hist->SetLineColor(kGreen);
            //noise_hist->Draw("same");

            canvas->Update();

            // Draw lines
            double xmin, ymin, xmax, ymax;
            canvas->GetRangeAxis(xmin, ymin, xmax, ymax);
            for(size_t time_i=0; time_i < time_frame_count; time_i++) {
                if(time_i == 0) continue;
                int line_x = time_i * adc_count;
                TLine *line = new TLine(line_x, ymin, line_x, ymax);
                line->SetLineColor(kGray); // Set the line color to red
                line->SetLineStyle(kDashed);
                line->Draw(); // Draw the line
            }

            // Description pave
            legend = new TLegend(0.1,0.9 - peaks.size()*0.05 ,0.3,0.9, "Peaks info");
            legend->SetFillColorAlpha(kWhite, 0);
            legend->SetBorderSize(1);
            legend->SetLineColor(kBlack);
            legend->SetTextAlign(12);
            legend->Clear();
            // Save canvas and TLegend

            peak_paves_by_plane[peak->plane_name] = legend;
        }
        else {
            canvas = peak_canvases_by_plane[peak->plane_name];
            legend = peak_paves_by_plane[peak->plane_name];
        }

        canvas->cd();
        double bin_width = data_hist->GetBinWidth(peak->index);
        double xmin = peak->index + (peak->time_id * adc_count) - peak->width/2.0 - 1.5;  // 1.5 - histo bins are aligned
        auto peak_box = new TBox(xmin, 0, xmin + peak->width, peak->height);
        peak_box->SetLineColor(kRed);
        peak_box->SetFillColorAlpha(kRed, 0.5);
        peak_box->Draw();

        auto peak_desc = fmt::format("Peak channel={}, width={}, height={:.1f}, time={}",
            peak->index, peak->width, peak->height, peak->time_id);
        // double xmin, ymin, xmax, ymax;
        // canvas->GetRangeAxis(xmin, ymin, xmax, ymax);
        legend->AddEntry((TObject*) nullptr, peak_desc.c_str());
        canvas->Update();
        // TPaveStats *ps = (TPaveStats*) canvas->GetPrimitive("stats");
        // ps->AddText(peak_desc.c_str());

        // delete c1;
    }

    // So the next has to do with cern root automated ownership
    for(auto pair: peak_canvases_by_plane) {
        auto plane_name = pair.first;
        auto canvas = pair.second;
        canvas->cd();
        auto pave = peak_paves_by_plane[plane_name];
        pave->Draw();
        canvas->Update();
        canvas->Write();
        delete pair.second;
    }

    for(auto pair: event_data_histos_by_plane) {
        //pair.second->Write();
    }
}

void ml4fpga::gem::GemReconDqmProcessor::FillIntegralPlaneData(const std::shared_ptr<const JEvent> &event) {


}

void ml4fpga::gem::GemReconDqmProcessor::FillEventClusters(const std::shared_ptr<const JEvent> &event)
{


}

void ml4fpga::gem::GemReconDqmProcessor::FillIntegralClusters(const std::shared_ptr<const JEvent> &event)
{
    auto clusters = event->Get<SFclust>();

    m_h1d_cluster_count->Fill(clusters.size());

    for(auto &cluster: clusters) {
        m_h1d_cluster_pos_x->Fill(cluster->pos_x);
        m_h1d_cluster_pos_y->Fill(cluster->pos_y);
        m_h2d_cluster_pos_xy->Fill(cluster->pos_x, cluster->pos_y);
        m_h1d_cluster_idx_x->Fill(cluster->index_x);
        m_h1d_cluster_idx_y->Fill(cluster->index_y);
        m_h2d_cluster_idx_xy->Fill(cluster->index_x, cluster->index_y);
        m_h2d_cluster_amp->Fill(cluster->amplitude);
        m_h2d_cluster_energy->Fill(cluster->energy);
    }
}

void ml4fpga::gem::GemReconDqmProcessor::FillEventPeaks(const std::shared_ptr<const JEvent> &event)
{
    gROOT->SetBatch(kFALSE);

}

void ml4fpga::gem::GemReconDqmProcessor::FillIntegralPeaks(const std::shared_ptr<const JEvent> &event)
{
    auto pf_result = event->GetSingle<PlanePeakFindingResult>();
    // Peak finding result or the configured planes may be absent - nothing to fill
    if (pf_result == nullptr
        || !pf_result->peaks_by_plane.count(m_name_plane_x)
        || !pf_result->peaks_by_plane.count(m_name_plane_y)) {
        return;
    }
    const auto& x_peaks = pf_result->peaks_by_plane.at(m_name_plane_x);
    const auto& y_peaks = pf_result->peaks_by_plane.at(m_name_plane_y);

    for(auto & x_peak: x_peaks) {
    }

    for(auto & y_peak: y_peaks) {

    }

    for(auto & x_peak: x_peaks) {
        for(auto & y_peak: y_peaks) {

        }
    }

    double plane_size_x = m_mapping->GetPlaneSize(m_name_plane_x);
    double plane_size_y = m_mapping->GetPlaneSize(m_name_plane_y);
}


void ml4fpga::gem::GemReconDqmProcessor::FillIntegralTimePeakData(const std::shared_ptr<const JEvent>& event) {
    // auto pf_result =
}

void ml4fpga::gem::GemReconDqmProcessor::FillSampleData(const std::shared_ptr<const JEvent> &event) {

    // The trick is that SampleData is guaranteed to be ordered by
    // detector -> plane -> time bin -> apv on plane -> channel num
    auto samples = event->Get<SampleData>();

    // Select samples by plane, the second map saves sorting in order apv
    std::unordered_map<uint64_t, std::map<uint64_t, const SampleData*>> m_samples_by_plane;
    for(auto sample: samples) {
        m_samples_by_plane[sample->plane][sample->id] = sample;
    }

    // Build plots for each plane:
    for(auto & [plane_id, plane_samples_map]: m_samples_by_plane) {
        auto plane_name = m_mapping->GetPlaneFromPlaneID(plane_id);

        std::string histo_name = fmt::format("{}_samples", plane_name);
        std::string histo_title = fmt::format("{} Sample data for event {}", plane_name, m_total_event_num);

    }



    //auto hists_dir = m_dqm_service->GetPerEventSubDir(m_total_event_num, "gem_raw");

    //auto event_data_histo = new TH1F(histo_name.c_str(), histo_title.c_str(), nbins, -0.5, nbins - 0.5);
    //auto event_noise_histo = new TH1F(("noise_" + histo_name).c_str(), histo_title.c_str(), nbins, -0.5, nbins - 0.5);


    // Fill the histogram
    // for(size_t time_i=0; time_i < time_frame_count; time_i++) {
    //     for(size_t adc_i=0; adc_i < adc_count; adc_i++) {
    //         int index = adc_count * time_i + adc_i;
    //         event_data_histo->SetBinContent(index, plane_data.data[time_i][adc_i]);
    //         event_noise_histo->SetBinContent(index, plane_data.PedestalNoises[adc_i]);
    //         integral_1d_histo->AddBinContent(index, plane_data.data[time_i][adc_i]);
    //         integral_2d_histo->Fill(adc_i, plane_data.data[time_i][adc_i]);
    //     }
    //
    //     if(time_i > 0) {
    //         event_data_histo->SetBinContent(adc_count * time_i, 0);
    //     }
    // }
}
