#pragma once

#include <JANA/JEventProcessor.h>
#include <JANA/JEventProcessorSequentialRoot.h>
#include <extensions/spdlog/SpdlogMixin.h>
#include <TDirectory.h>
#include <TH1F.h>
#include <TH2F.h>
#include <TTree.h>
#include "SrsRawRecord.h"
#include "F125FDCPulseRecord.h"
#include "F125WindowRawRecord.h"
#include "rawdataparser/Df125FDCPulse.h"
#include "rawdataparser/DGEMSRSWindowRawData.h"
#include "rawdataparser/Df125WindowRawData.h"
#include "rawdataparser/Df250WindowRawData.h"
#include "F250WindowRawRecord.h"
#include "GemSimpleCluster.h"
#include "F250FDCPulseRecord.h"
#include "rawdataparser/Df250PulseData.h"
#include "plugins/gemrecon/DecodedData.h"
#include "plugins/gemrecon/SFclust.h"
#include "SrsPreReconRecord.h"
#include "GemPlanePeak.h"
#include <plugins/gemrecon/PlanePeak.h>

#include "FpgaF125Cluster.h"
#include "FpgaHitToTrack.h"
#include "FpgaTrackFit.h"
#include "GemSampleData.h"
#include "plugins/fpgacon/F125Cluster.h"
#include "plugins/fpgacon/FpgaHitsToTrack.h"
#include "plugins/fpgacon/FpgaTrackFit.h"
#include "plugins/gemrecon/SampleData.h"

class JEvent;
class JApplication;

class FlatTreeWriterProcessor:
        public JEventProcessor,
        public spdlog::extensions::SpdlogMixin<FlatTreeWriterProcessor>   // this automates proper Log initialization
{
public:
    explicit FlatTreeWriterProcessor(JApplication *);
    ~FlatTreeWriterProcessor() override = default;

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

    // TODO there should be a FlatIO for each worker thread
    std::recursive_mutex io_mutex;
    TTree *mEventTree;
    std::vector<std::reference_wrapper<flatio::AlignedArraysIO>> m_ios;

    flatio::SrsRawRecordIO m_srs_record_io;
    flatio::F125WindowRawRecordIO m_f125_wraw_io;
    flatio::F250WindowRawRecordIO m_f250_wraw_io;
    flatio::F125FDCPulseRecordIO m_f125_pulse_io;
    flatio::F250FDCPulseRecordIO m_f250_pulse_io;
    flatio::GemSimpleClusterIO m_gem_scluster_io;
    flatio::SrsPreReconRecordIO m_srs_prerecon_io;
    flatio::GemPlanePeakIO m_gem_peak_io;
    flatio::GemSampleDataIO m_gem_sample_data_io;
    flatio::FpgaF125ClusterIO m_fpga_f125_cluster_io;
    flatio::FpgaHitToTrackIO m_fpga_hits_to_track_io;
    flatio::FpgaTrackFitIO m_fpga_track_fit_io;


    std::shared_ptr<JGlobalRootLock> m_glb_root_lock;

    uint16_t findBestSrsSamle(std::vector<uint16_t> samples);

    void SaveF125FDCPulse(const std::vector<const Df125FDCPulse *>& records);
    void SaveF250FDCPulse(const std::vector<const Df250PulseData *>& records);
    void SaveGEMSRSWindowRawData(std::vector<const DGEMSRSWindowRawData *> records);
    void SaveF125WindowRawData(std::vector<const Df125WindowRawData *> records);
    void SaveF250WindowRawData(std::vector<const Df250WindowRawData *> records);
    void SaveGEMSimpleClusters(std::vector<const ml4fpga::gem::SFclust *> clusters);
    void SaveFPGAClusters(const std::vector<const ml4fpga::fpgacon::F125Cluster *> & clusters);
    void SaveFPGAHitsToTracks(const std::vector<const ml4fpga::fpgacon::FpgaHitsToTrack *> & ht_assocs);
    void SaveFPGATrackFits(const std::vector<const ml4fpga::fpgacon::FpgaTrackFit *> & tfits);
    void SaveGEMSampleData(const std::vector<const ml4fpga::gem::SampleData *> & samples);



//    void SaveGEMDecodedData(const ml4fpga::gem::DecodedData *pData);
    TDirectory* m_main_dir;

    void SaveGEMDecodedData(const ml4fpga::gem::PlaneDecodedData *data);
    void SaveGEMPlanePeak(const std::vector<const ml4fpga::gem::PlanePeak *> &peaks);
};

