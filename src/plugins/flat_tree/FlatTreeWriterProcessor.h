#pragma once

#include <JANA/JEventProcessor.h>
#include <JANA/Services/JGlobalRootLock.h>
#include <atomic>
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

/// Selects which branch groups the events tree contains (-Pflat_tree:write_*).
///
/// Output size is completely dominated by the two PER-SAMPLE groups; the rest are
/// per-cluster/peak and negligible. Rough uncompressed cost per event for a
/// typical SRS setup (1536 channel records x 3 time bins):
///
///   gem_sample_data  ~300 kB/event  (65 B x n_channels x n_timebins; 4 doubles
///                                    per sample, which also compress poorly)
///   srs_raw           ~50 kB/event  (raw ADC samples + channel indices)
///   everything else    <1 kB/event
///
/// So a full-detail run costs ~350 kB/event - hundreds of GB for a multi-million
/// event run. Turn off what you do not analyze: keeping only the reconstruction
/// output (clusters/peaks) shrinks the tree by ~3 orders of magnitude.
///
/// fpga defaults to OFF because the 'fpgacon' plugin is retired, so those
/// branches would be written empty for every event.
/// Defaults follow what the analysis/DQM work actually reads (L. Kasper, Aug 2026):
/// f125_wraw, f250_wraw, f125_pulse, f250_pulse and gem_peak are used; srs_raw and
/// srs_prerecon are not; gem_sample_data and fpga_* are not used by that analysis.
/// Turning the unused groups off takes a 10M-event run from ~933 GB to ~27 GB.
struct FlatIoConfig {
    bool srs_raw      = false;  ///< srs_raw_*          RAW SRS samples - not used by analysis
    bool f125_raw     = true;   ///< f125_wraw_*        USED
    bool f250_raw     = true;   ///< f250_wraw_*        USED
    bool f125_pulse   = true;   ///< f125_pulse_*       USED
    bool f250_pulse   = true;   ///< f250_pulse_*       USED
    bool gem_clusters = true;   ///< gem_scluster_*     reconstruction output (tiny)
    bool gem_peaks    = true;   ///< gem_peak_*         USED
    bool gem_prerecon = false;  ///< srs_prerecon_*     decoded plane profiles - not used
    bool gem_samples  = true;   ///< gem_sample_data_*  DIAGNOSTIC: capped to the first
                                ///<                    gem_samples_max_events events, so it
                                ///<                    costs a fixed ~38 MB per run
    bool fpga         = false;  ///< fpga_*             retired plugin -> empty
};

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

    FlatIoConfig m_config;

    /// Only the ENABLED groups. Disabled ones are never bound, so their branches
    /// do not exist in the tree at all - and because AlignedArraysIO::add()
    /// throws when unbound, Process() must skip their Save*() calls to match.
    std::vector<std::reference_wrapper<AlignedArraysIO>> m_ios;

    explicit FlatIoBundle(const FlatIoConfig& config = {}) : m_config(config) {
        if (m_config.srs_raw)      m_ios.push_back(m_srs_record_io);
        if (m_config.f125_raw)     m_ios.push_back(m_f125_wraw_io);
        if (m_config.f250_raw)     m_ios.push_back(m_f250_wraw_io);
        if (m_config.f125_pulse)   m_ios.push_back(m_f125_pulse_io);
        if (m_config.f250_pulse)   m_ios.push_back(m_f250_pulse_io);
        if (m_config.gem_clusters) m_ios.push_back(m_gem_scluster_io);
        if (m_config.gem_prerecon) m_ios.push_back(m_srs_prerecon_io);
        if (m_config.gem_peaks)    m_ios.push_back(m_gem_peak_io);
        if (m_config.gem_samples)  m_ios.push_back(m_gem_sample_data_io);
        if (m_config.fpga) {
            m_ios.push_back(m_fpga_f125_cluster_io);
            m_ios.push_back(m_fpga_hits_to_track_io);
            m_ios.push_back(m_fpga_track_fit_io);
        }
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
        explicit WriterCtx(const flatio::FlatIoConfig& config) : io(config) {}

        std::shared_ptr<ROOT::TBufferMergerFile> file;  // null in legacy mode
        TTree* tree = nullptr;
        flatio::FlatIoBundle io;
        size_t fills_since_flush = 0;
    };

    /// Returns this thread's writer context, creating it on first use (MT mode),
    /// or the single legacy context.
    WriterCtx& GetCtx();

    flatio::FlatIoConfig m_io_config; ///< params flat_tree:write_* (branch groups)

    /// gem_sample_data is per-sample diagnostic data (~300 kB/event) that is
    /// normally only wanted for a sample of events to plot waveforms/baselines in
    /// retrospect - not for every event of a multi-million event run. Writing it
    /// stops after this many events (0 = no limit); later events simply get an
    /// empty gem_sample_data_count. Counted with an atomic because Process() runs
    /// concurrently, so the cutoff is approximate to within one event per thread.
    /// Default 500: enough to plot waveforms/baselines in retrospect, while a
    /// full multi-million event run no longer pays ~300 kB/event for data nobody
    /// analyses event-by-event. Set to 0 for the old write-every-event behaviour.
    size_t m_gem_samples_max_events = 500;
    std::atomic<size_t> m_gem_samples_seen{0};

    /// Measured on run 8169: only ~1.2% of samples are above the n-sigma noise
    /// threshold - the other ~98.8% are baseline, stored as 4 doubles each. With
    /// this on, only samples with is_noise==false are written, which keeps the
    /// signal for EVERY event at ~1% of the cost. Off by default because pedestal
    /// and baseline studies need the noise samples too; that is what the
    /// gem_samples_max_events window preserves.
    bool m_gem_samples_signal_only = false;

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
