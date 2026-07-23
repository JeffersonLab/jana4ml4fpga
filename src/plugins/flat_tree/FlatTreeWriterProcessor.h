#pragma once

#include <JANA/JEventProcessor.h>
#include <JANA/Services/JGlobalRootLock.h>
#include <memory>
#include <spdlog/logger.h>
#include <TDirectory.h>
#include <TH1F.h>
#include <TH2F.h>
#include <TTree.h>
#include <ROOT/TBufferMerger.hxx>
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

namespace flatio {

/// One complete set of tree-writer IO buffers. In multithreaded mode each worker
/// thread owns one bundle bound to its own (in-memory) tree; in single-thread
/// (legacy) mode there is exactly one, bound to the tree in the shared hists file.
struct FlatIoBundle {
    SrsRawRecordIO m_srs_record_io;
    F125WindowRawRecordIO m_f125_wraw_io;
    F250WindowRawRecordIO m_f250_wraw_io;
    F125FDCPulseRecordIO m_f125_pulse_io;
    F250FDCPulseRecordIO m_f250_pulse_io;
    GemSimpleClusterIO m_gem_scluster_io;
    SrsPreReconRecordIO m_srs_prerecon_io;
    GemPlanePeakIO m_gem_peak_io;
    GemSampleDataIO m_gem_sample_data_io;
    FpgaF125ClusterIO m_fpga_f125_cluster_io;
    FpgaHitToTrackIO m_fpga_hits_to_track_io;
    FpgaTrackFitIO m_fpga_track_fit_io;

    /// Event number leaf: with concurrent writers tree entry ORDER is not event
    /// order - consumers must sort/select by event_number (MT plan, hazard H6).
    ULong64_t m_event_number = 0;

    std::vector<std::reference_wrapper<AlignedArraysIO>> m_ios;

    FlatIoBundle() {
        m_ios = {m_srs_record_io,    m_f125_wraw_io,        m_f250_wraw_io,
                 m_f125_pulse_io,    m_f250_pulse_io,       m_gem_scluster_io,
                 m_srs_prerecon_io,  m_gem_peak_io,         m_gem_sample_data_io,
                 m_fpga_f125_cluster_io, m_fpga_hits_to_track_io, m_fpga_track_fit_io};
    }

    void bindToTree(TTree* tree) {
        tree->Branch("event_number", &m_event_number, "event_number/l");
        for (auto& io : m_ios) io.get().bindToTree(tree);
    }

    void clear() {
        for (auto& io : m_ios) io.get().clear();
    }
};

}  // namespace flatio

class FlatTreeWriterProcessor:
        public JEventProcessor
{
public:
    explicit FlatTreeWriterProcessor(JApplication *);
    ~FlatTreeWriterProcessor() override = default;

    void Init() override;

    /// Called concurrently from JANA worker threads. In MT mode each thread fills
    /// its own TBufferMerger tree lock-free; in legacy mode (nthreads==1) the
    /// single bundle/tree in the shared hists file is used, exactly as before.
    void Process(const std::shared_ptr<const JEvent>& event) override;

    void Finish() override;

private:
    /// Per-thread writer context (MT mode). file/tree live in the TBufferMerger.
    struct WriterCtx {
        std::shared_ptr<ROOT::TBufferMergerFile> file;  // null in legacy mode
        TTree* tree = nullptr;
        flatio::FlatIoBundle io;
        size_t fills_since_flush = 0;
    };

    /// Returns this thread's writer context, creating it on first use (MT mode),
    /// or the single legacy context.
    WriterCtx& GetCtx();

    bool m_mt_mode = false;
    std::string m_mt_output;          ///< param flat_tree:mt_output ("" = auto name)
    size_t m_flush_events = 10000;    ///< param flat_tree:flush_events (per thread)

    std::unique_ptr<ROOT::TBufferMerger> m_merger;   // MT mode only
    std::mutex m_ctx_mutex;
    std::vector<std::unique_ptr<WriterCtx>> m_contexts;

    WriterCtx* m_legacy_ctx = nullptr;               // legacy mode only
    TDirectory* m_main_dir = nullptr;                // legacy mode: shared hists file dir

    std::shared_ptr<JGlobalRootLock> m_glb_root_lock;
    std::shared_ptr<spdlog::logger> m_log;   // aspect logger: "out"

    uint16_t findBestSrsSamle(std::vector<uint16_t> samples);

    void SaveF125FDCPulse(flatio::FlatIoBundle& io, const std::vector<const Df125FDCPulse *>& records);
    void SaveF250FDCPulse(flatio::FlatIoBundle& io, const std::vector<const Df250PulseData *>& records);
    void SaveGEMSRSWindowRawData(flatio::FlatIoBundle& io, std::vector<const DGEMSRSWindowRawData *> records);
    void SaveF125WindowRawData(flatio::FlatIoBundle& io, std::vector<const Df125WindowRawData *> records);
    void SaveF250WindowRawData(flatio::FlatIoBundle& io, std::vector<const Df250WindowRawData *> records);
    void SaveGEMSimpleClusters(flatio::FlatIoBundle& io, std::vector<const ml4fpga::gem::SFclust *> clusters);
    void SaveFPGAClusters(flatio::FlatIoBundle& io, const std::vector<const ml4fpga::fpgacon::F125Cluster *> & clusters);
    void SaveFPGAHitsToTracks(flatio::FlatIoBundle& io, const std::vector<const ml4fpga::fpgacon::FpgaHitsToTrack *> & ht_assocs);
    void SaveFPGATrackFits(flatio::FlatIoBundle& io, const std::vector<const ml4fpga::fpgacon::FpgaTrackFit *> & tfits);
    void SaveGEMSampleData(flatio::FlatIoBundle& io, const std::vector<const ml4fpga::gem::SampleData *> & samples);
    void SaveGEMDecodedData(flatio::FlatIoBundle& io, const ml4fpga::gem::PlaneDecodedData *data);
    void SaveGEMPlanePeak(flatio::FlatIoBundle& io, const std::vector<const ml4fpga::gem::PlanePeak *> &peaks);

    // GEM plane names used for srs_prerecon branches. Shared knob with gemrecon
    // plugin (same parameter names): -Pgemrecon:plane_name_x/_y
    std::string m_gem_plane_x = "URWELLX";
    std::string m_gem_plane_y = "URWELLY";
};
