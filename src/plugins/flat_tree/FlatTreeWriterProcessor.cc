#include "SrsRawRecord.h"

#include "FlatTreeWriterProcessor.h"
#include "rawdataparser/DGEMSRSWindowRawData.h"
#include "rawdataparser/Df125WindowRawData.h"
#include "rawdataparser/Df125FDCPulse.h"
#include "rawdataparser/Df250PulseData.h"
#include "rawdataparser/Df125Config.h"
#include "rawdataparser/Df250WindowRawData.h"
#include "F250WindowRawRecord.h"
#include "plugins/gemrecon/DecodedData.h"

#include "plugins/gemrecon/SFclust.h"

#include <JANA/JApplication.h>
#include <JANA/JEvent.h>

#include <Math/GenVector/PxPyPzM4D.h>

#include <spdlog/spdlog.h>
#include <services/root_output/RootFile_service.h>
#include <plugins/gemrecon/Constants.h>

#include "plugins/fpgacon/F125Cluster.h"
#include "plugins/fpgacon/FpgaHitsToTrack.h"
#include "plugins/fpgacon/FpgaTrackFit.h"
#include "plugins/gemrecon/SampleData.h"

//------------------
// OccupancyAnalysis (Constructor)
//------------------
FlatTreeWriterProcessor::FlatTreeWriterProcessor(JApplication *app) :
        JEventProcessor(app) {
}

//------------------
// Init
//------------------
void FlatTreeWriterProcessor::Init() {

    // Get JANA application
    auto app = GetApplication();

    // Get Log level from user parameter or default
    InitLogger(GetPluginName());
    logger()->info("This plugin name is: " + GetPluginName());

    // Ask service locator a file to write histograms to
    auto root_file_service = app->GetService<RootFile_service>();

    // Get TDirectory for histograms root file
    // m_glb_root_lock = app->GetService<JGlobalRootLock>();
    // m_glb_root_lock->acquire_write_lock();
    try {
        auto file = root_file_service->GetHistFile();
        file->cd();
        m_main_dir = gDirectory;

        mEventTree = new TTree("events","jana4ml4fpga_tree_v1");
        m_ios.push_back(m_srs_record_io);
        m_ios.push_back(m_f125_wraw_io);
        m_ios.push_back(m_f250_wraw_io);
        m_ios.push_back(m_f125_pulse_io);
        m_ios.push_back(m_f250_pulse_io);
        m_ios.push_back(m_gem_scluster_io);
        m_ios.push_back(m_srs_prerecon_io);
        m_ios.push_back(m_gem_peak_io);
        m_ios.push_back(m_gem_sample_data_io);
        m_ios.push_back(m_fpga_f125_cluster_io);
        m_ios.push_back(m_fpga_hits_to_track_io);
        m_ios.push_back(m_fpga_track_fit_io);
        mEventTree->SetDirectory(m_main_dir);

        for(auto &io: m_ios) {
            io.get().bindToTree(mEventTree);
        }
        // m_glb_root_lock->release_lock();
    }
    catch (...) {
        // m_glb_root_lock->release_lock();
        // Ideally we'd use the STL read-write lock (std::shared_mutex/lock) intead of the pthreads one.
        // However, for now we are limited to C++14 (we would need C++17).
        throw;
        // It is important to re-throw exceptions so that the framework can handle them correctly
    }

    logger()->info("Initialization is done");
}


//------------------
// Process
//------------------
// This function is called every event
void FlatTreeWriterProcessor::Process(const std::shared_ptr<const JEvent> &event) {

    // TODO // m_glb_root_lock->acquire_write_lock();
    try {
        m_log->debug("=======================");
        m_log->debug("Event number {}", event->GetEventNumber());

        // Clear writing classes on new event
        for(auto &io: m_ios) {
            io.get().clear();
        }

        // Print what we have in events
        if(m_log->level() <= spdlog::level::debug) {
            m_log->debug("Available data (factories actually)");
            m_log->debug("    {:<30}  {}", "[name]", "[objects count]");
            for (auto factory: event->GetFactorySet()->GetAllFactories()) {
                m_log->debug("    {:<30}  {}",  factory->GetObjectName(), factory->GetNumObjects());
            }
        }

        bool has_srs_window_raw_data = false;
        bool has_f125_pulse_data = false;
        bool has_f250_pulse_data = false;
        bool has_f250_window_raw_data = false;
        bool has_f125_window_raw_data = false;


        // PASS 1 GO OVER EVIO DATA
        for (auto factory: event->GetFactorySet()->GetAllFactories()) {
            const std::string &obj_name = factory->GetObjectName();
            auto obj_num = factory->GetNumObjects();

            if(JTypeInfo::demangle<Df125FDCPulse>() == obj_name && obj_num > 0) {
                SaveF125FDCPulse(event->Get<Df125FDCPulse>());
                has_f125_pulse_data = true;
            }

            if(JTypeInfo::demangle<Df250PulseData>() == obj_name && obj_num > 0) {
                SaveF250FDCPulse(event->Get<Df250PulseData>());
                has_f250_pulse_data = true;
            }

            if(JTypeInfo::demangle<Df125WindowRawData>() == obj_name && obj_num > 0) {
                SaveF125WindowRawData(event->Get<Df125WindowRawData>());
                has_f125_window_raw_data = true;
            }

            if(JTypeInfo::demangle<Df250WindowRawData>() == obj_name && obj_num > 0) {
                SaveF250WindowRawData(event->Get<Df250WindowRawData>());
                has_f250_window_raw_data = true;
            }

            if(JTypeInfo::demangle<DGEMSRSWindowRawData>() == obj_name && obj_num > 0) {
                SaveGEMSRSWindowRawData(event->Get<DGEMSRSWindowRawData>());
                has_srs_window_raw_data = true;
            }
        }

        // GO OVER FACTORY GENERATED DATA
        for (auto factory: event->GetFactorySet()->GetAllFactories()) {
            const std::string &obj_name = factory->GetObjectName();
            const std::string jana_demangle = JTypeInfo::demangle<ml4fpga::fpgacon::F125Cluster>();

            if(obj_name == jana_demangle && event->GetEventNumber() > 4) {
                auto f125_clusters = event->Get<ml4fpga::fpgacon::F125Cluster>();
                logger()->trace("has F125Cluster");
            }

            // PLUGIN 'gemrecon` data
            if(has_srs_window_raw_data && obj_name == JTypeInfo::demangle<ml4fpga::gem::PlanePeak>()) {
                try
                {
                    // TODO fix it and check for factory
                    auto clusters = event->Get<ml4fpga::gem::SFclust>();
                    SaveGEMSimpleClusters(clusters);
                    auto plane_decoded_data = event->GetSingle<ml4fpga::gem::PlaneDecodedData>();
                    SaveGEMDecodedData(plane_decoded_data);
                    auto peaks = event->Get<ml4fpga::gem::PlanePeak>();
                    SaveGEMPlanePeak(peaks);
                    auto samples = event->Get<ml4fpga::gem::SampleData>();
                    SaveGEMSampleData(samples);
                }
                catch(std::exception& ex) {
                    m_log->error("Problem saving gemercon data problem: {}", ex.what());
                }
            }

            // PLUGIN 'fpgacon` data
            if(has_f125_window_raw_data && obj_name == JTypeInfo::demangle<ml4fpga::fpgacon::F125Cluster>()) {
                try
                {
                    // TODO fix it and check for factory
                    auto clusters = event->Get<ml4fpga::fpgacon::F125Cluster>();
                    SaveFPGAClusters(clusters);
                    auto hit_track_assocs = event->Get<ml4fpga::fpgacon::FpgaHitsToTrack>();
                    SaveFPGAHitsToTracks(hit_track_assocs);
                    auto track_fits = event->Get<ml4fpga::fpgacon::FpgaTrackFit>();
                    SaveFPGATrackFits(track_fits);
                }
                catch(std::exception& ex) {
                    m_log->error("Problem saving fpgacon data problem: {}", ex.what());
                }
            }
        }

        // Fill the tree
        m_main_dir->cd();
        mEventTree->Fill();
        // m_glb_root_lock->release_lock();
    }
    catch (...) {
        // m_glb_root_lock->release_lock();
        // Ideally we'd use the STL read-write lock (std::shared_mutex/lock) intead of the pthreads one.
        // However, for now we are limited to C++14 (we would need C++17).
        throw;
        // It is important to re-throw exceptions so that the framework can handle them correctly
    }

    m_log->debug("Event number {}", event->GetEventNumber());
}

//------------------
// Finish
//------------------
void FlatTreeWriterProcessor::Finish() {

    try {
        m_main_dir->cd();
        mEventTree->Write();
        // m_glb_root_lock->release_lock();
    }
    catch (...) {
        // m_glb_root_lock->release_lock();
        throw;
    }
}

uint16_t FlatTreeWriterProcessor::findBestSrsSamle(std::vector<uint16_t> samples) {
    // Examining data, there might be the next number of samples:
    // 1, 3, 4, n
    if(samples.size() == 0) {
        // empty array
        return 0;
    }

    if(samples.size() == 4) {
        // when 4 values are saved, the first is wrongly big, and 3 are to be chosen from
        std::vector<uint16_t> real_samples = {samples[1], samples[2], samples[3]};
        return findBestSrsSamle(real_samples);
    }

    // If we are here, just find the biggest sample
    uint16_t best_sample = samples[0];
    for(auto sample: samples) {
        if(sample > best_sample) best_sample = sample;
    }
    return best_sample;
}

void FlatTreeWriterProcessor::SaveF125FDCPulse(const std::vector<const Df125FDCPulse *>& records) {
    for (auto record: records) {
        flatio::F125FDCPulseRecord save_struct{};
        save_struct.roc = record->rocid;
        save_struct.slot = record->slot;
        save_struct.channel = record->channel;
        save_struct.npk = record->NPK;
        save_struct.le_time = record->le_time;
        save_struct.time_quality_bit = record->time_quality_bit;
        save_struct.overflow_count = record->overflow_count;
        save_struct.pedestal = record->pedestal;
        save_struct.integral = record->integral;
        save_struct.peak_amp = record->peak_amp;
        save_struct.peak_time = record->peak_time;
        save_struct.word1 = record->word1;
        save_struct.word2 = record->word2;
        save_struct.nsamples_pedestal = record->nsamples_pedestal;
        save_struct.nsamples_integral = record->nsamples_integral;
        save_struct.emulated = record->emulated;
        save_struct.le_time_emulated = record->le_time_emulated;
        save_struct.time_quality_bit_emulated = record->time_quality_bit_emulated;
        save_struct.overflow_count_emulated = record->overflow_count_emulated;
        save_struct.pedestal_emulated = record->pedestal_emulated;
        save_struct.integral_emulated = record->integral_emulated;
        save_struct.peak_amp_emulated = record->peak_amp_emulated;
        save_struct.peak_time_emulated = record->peak_time_emulated;
        m_f125_pulse_io.add(save_struct);
    }
}

void FlatTreeWriterProcessor::SaveF250FDCPulse(const std::vector<const Df250PulseData *>& records) {
    for (auto record: records) {
        flatio::F250FDCPulseRecord save_struct{};
        save_struct.roc = record->rocid;
        save_struct.slot = record->slot;
        save_struct.channel = record->channel;
        save_struct.event_within_block = record->event_within_block;
        save_struct.qf_pedestal = record->QF_pedestal;
        save_struct.pedestal = record->pedestal;
        save_struct.integral = record->integral;
        save_struct.qf_nsa_beyond_ptw = record->QF_NSA_beyond_PTW;
        save_struct.qf_overflow = record->QF_overflow;
        save_struct.qf_underflow = record->QF_underflow;
        save_struct.nsamples_over_threshold = record->nsamples_over_threshold;
        save_struct.course_time = record->course_time;
        save_struct.fine_time = record->fine_time;
        save_struct.pulse_peak = record->pulse_peak;
        save_struct.qf_vpeak_beyond_nsa = record->QF_vpeak_beyond_NSA;
        save_struct.qf_vpeak_not_found = record->QF_vpeak_not_found;
        save_struct.qf_bad_pedestal = record->QF_bad_pedestal;
        save_struct.pulse_number = record->pulse_number;
        save_struct.nsamples_integral = record->nsamples_integral;
        save_struct.nsamples_pedestal = record->nsamples_pedestal;
        save_struct.emulated = record->emulated;
        save_struct.integral_emulated = record->integral_emulated;
        save_struct.pedestal_emulated = record->pedestal_emulated;
        save_struct.time_emulated = record->time_emulated;
        save_struct.course_time_emulated = record->course_time_emulated;
        save_struct.fine_time_emulated = record->fine_time_emulated;
        save_struct.pulse_peak_emulated = record->pulse_peak_emulated;
        save_struct.qf_emulated = record->QF_emulated;
        m_f250_pulse_io.add(save_struct);
    }
}

void FlatTreeWriterProcessor::SaveGEMSRSWindowRawData(std::vector<const DGEMSRSWindowRawData *> records) {
    m_log->trace("Writing DGEMSRSWindowRawData data items: {} ", records.size());
    for(auto srs_item: records) {
        flatio::SrsRawRecord srs_save{};
        srs_save.roc = srs_item->rocid;
        srs_save.slot = srs_item->slot;
        srs_save.channel = srs_item->channel;
        srs_save.apv_id = srs_item->apv_id;
        srs_save.channel_apv = ml4fpga::gem::Constants::ApvChannelCorrection(srs_item->channel_apv);

        for(auto sample: srs_item->samples) {
            srs_save.samples.push_back(sample);
        }
        m_srs_record_io.add(srs_save);
    }
}


void FlatTreeWriterProcessor::SaveF125WindowRawData(std::vector<const Df125WindowRawData *> records) {
    for (auto record: records) {
        flatio::F125WindowRawRecord f125_wraw_save{};
        f125_wraw_save.roc = record->rocid;
        f125_wraw_save.slot = record->slot;
        f125_wraw_save.channel = record->channel;
        f125_wraw_save.overflow = record->overflow;
        f125_wraw_save.invalid_samples = record->invalid_samples;
        f125_wraw_save.itrigger = record->itrigger;
        f125_wraw_save.samples =  record->samples;
        m_f125_wraw_io.add(f125_wraw_save);
    }
}


void FlatTreeWriterProcessor::SaveF250WindowRawData(std::vector<const Df250WindowRawData *> records) {
    for (auto record: records) {
        flatio::F250WindowRawRecord f250_wraw_save{};
        f250_wraw_save.roc = record->rocid;
        f250_wraw_save.slot = record->slot;
        f250_wraw_save.channel = record->channel;
        f250_wraw_save.overflow = record->overflow;
        f250_wraw_save.invalid_samples = record->invalid_samples;
        f250_wraw_save.itrigger = record->itrigger;
        f250_wraw_save.samples =  record->samples;
        m_f250_wraw_io.add(f250_wraw_save);
    }
}


void FlatTreeWriterProcessor::SaveGEMSimpleClusters(std::vector<const ml4fpga::gem::SFclust *> clusters) {
    for(auto cluster: clusters) {
        flatio::GemSimpleCluster cluster_save;
        cluster_save.x = cluster->pos_x;
        cluster_save.y = cluster->pos_y;
        cluster_save.energy = cluster->energy;
        cluster_save.adc = cluster->amplitude;
        m_gem_scluster_io.add(cluster_save);
    }
}


void FlatTreeWriterProcessor::SaveGEMDecodedData(const ml4fpga::gem::PlaneDecodedData *data) {
    const auto& plane_data_x = data->plane_data.at("URWELLX");
    const auto& plane_data_y = data->plane_data.at("URWELLY");

    for(size_t time_i=0; time_i < plane_data_x.data.size(); time_i++) {
        for(size_t adc_i=0; adc_i < plane_data_x.data[time_i].size(); adc_i++) {
            flatio::SrsPreReconRecord record;
            record.x = plane_data_x.data[time_i][adc_i];
            record.y = plane_data_y.data[time_i][adc_i];
            m_srs_prerecon_io.add(record);
        }
    }
}

void FlatTreeWriterProcessor::SaveGEMPlanePeak(const std::vector<const ml4fpga::gem::PlanePeak *> &peaks) {
    for(const auto peak: peaks) {
        flatio::GemPlanePeak save_peak;
        save_peak.apv_id = peak->apv_id;
        save_peak.plane_id = peak->plane_id;
        save_peak.time_id = peak->time_id;
        save_peak.plane_name = peak->plane_name;
        save_peak.height = peak->height;
        save_peak.width = peak->width;
        save_peak.area = peak->area;
        save_peak.index = peak->index;
        save_peak.real_pos = peak->real_pos;
        m_gem_peak_io.add(save_peak);
    }
}

void FlatTreeWriterProcessor::SaveGEMSampleData(const std::vector<const ml4fpga::gem::SampleData *> &samples) {
    for(const auto sample:samples) {
        flatio::GemSampleData sample_save;
        sample_save.id = sample->id;
        sample_save.channel = sample->channel;
        sample_save.raw_channel = sample->raw_channel;
        sample_save.time_bin = sample->time_bin;
        sample_save.apv = sample->apv;
        sample_save.plane = sample->plane;
        sample_save.detector = sample->detector;
        sample_save.is_noise = sample->is_noise;
        sample_save.value = sample->value;
        sample_save.raw_value = sample->raw_value;
        sample_save.rolling_average = sample->rolling_average;
        sample_save.rolling_std = sample->rolling_std;
        m_gem_sample_data_io.add(sample_save);
    }
}

void FlatTreeWriterProcessor::SaveFPGAClusters(const std::vector<const ml4fpga::fpgacon::F125Cluster *> &clusters) {
    for(const auto cluster: clusters) {
        flatio::FpgaF125Cluster cluster_save;
        cluster_save.id = cluster->id;
        cluster_save.pos_x = cluster->pos_x;
        cluster_save.pos_y = cluster->pos_y;
        cluster_save.pos_z = cluster->pos_z;
        cluster_save.dedx = cluster->dedx;
        cluster_save.size = cluster->size;
        cluster_save.width_y1 = cluster->width[0];
        cluster_save.width_y2 = cluster->width[1];
        cluster_save.width_dy = cluster->width[2];
        cluster_save.length_x1 = cluster->length[0];
        cluster_save.length_x2 = cluster->length[1];
        cluster_save.length_dx = cluster->length[2];
        m_fpga_f125_cluster_io.add(cluster_save);
    }
}

void FlatTreeWriterProcessor::SaveFPGAHitsToTracks(const std::vector<const ml4fpga::fpgacon::FpgaHitsToTrack *> &ht_assocs) {
    for(const auto hit_track_assoc: ht_assocs) {
        flatio::FpgaHitToTrack ht_save;
        ht_save.hit_index = hit_track_assoc->hit_index;
        ht_save.track_index = hit_track_assoc->track_index;
        m_fpga_hits_to_track_io.add(ht_save);
    }
}

void FlatTreeWriterProcessor::SaveFPGATrackFits(const std::vector<const ml4fpga::fpgacon::FpgaTrackFit *> &tfits) {
    for(const auto tfit: tfits) {
        flatio::FpgaTrackFit trk_fit_save;
        trk_fit_save.id = tfit->track_id;
        trk_fit_save.slope = tfit->slope;
        trk_fit_save.intersect = tfit->intersect;
        m_fpga_track_fit_io.add(trk_fit_save);
    }
}




