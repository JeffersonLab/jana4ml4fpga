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
#include <Compression.h>   // ROOT::CompressionSettings / ROOT::kLZ4

#include <spdlog/spdlog.h>
#include <services/root_output/RootFile_service.h>
#include <plugins/gemrecon/Constants.h>

#include "plugins/fpgacon/F125Cluster.h"
#include "plugins/fpgacon/FpgaHitsToTrack.h"
#include "plugins/fpgacon/FpgaTrackFit.h"
#include "plugins/gemrecon/SampleData.h"

//------------------
// FlatTreeWriterProcessor (Constructor)
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

    // Aspect logger (see Log_service): persistence/output aspect
    m_log = app->GetService<Log_service>()->logger("out");

    // GEM plane names for srs_prerecon branches. Same parameter keys as gemrecon
    // plugin so a single -Pgemrecon:plane_name_x/_y sets both plugins consistently.
    app->SetDefaultParameter("gemrecon:plane_name_x", m_gem_plane_x, "X Plane name (like URWELLX)");
    app->SetDefaultParameter("gemrecon:plane_name_y", m_gem_plane_y, "Y Plane name (like URWELLY)");

    // --- Writer mode selection (MT plan, workstream W1) -------------------------
    // nthreads==1  -> legacy mode: one tree inside the shared hists file (bit-
    //                 compatible with previous single-thread output).
    // nthreads!=1  -> MT mode: per-thread trees via ROOT::TBufferMerger, merged
    //                 into a separate single file. Entry order is NOT event order;
    //                 use the event_number leaf (added in both modes).
    std::string nthreads_str = "1";
    app->SetDefaultParameter("nthreads", nthreads_str, "Number of worker threads");
    app->SetDefaultParameter("flat_tree:mt_output", m_mt_output,
                             "Events-tree output file for multithreaded mode. "
                             "Empty = <histsfile without .root>_events.root");
    // --- Branch group selection (see flatio::FlatIoConfig for sizes) ------------
    // Defaults match what the analysis/DQM actually reads; srs_raw and
    // srs_prerecon are off because nobody uses them and together they cost
    // ~11 kB/event (~110 GB over a 10M-event run). Turn any group back on with
    // -Pflat_tree:write_<group>=1.
    app->SetDefaultParameter("flat_tree:write_srs_raw", m_io_config.srs_raw,
                             "Write srs_raw_* branches (raw SRS samples, ~50 kB/event)");
    app->SetDefaultParameter("flat_tree:write_gem_samples", m_io_config.gem_samples,
                             "Write gem_sample_data_* branches (per-sample GEM diagnostics, "
                             "~300 kB/event - the largest group by far)");
    app->SetDefaultParameter("flat_tree:gem_samples_max_events", m_gem_samples_max_events,
                             "Write gem_sample_data_* only for the first N events "
                             "(0 = every event). Use this to keep per-sample diagnostics "
                             "for plotting on a subset without paying ~300 kB for every "
                             "event of a full run");
    app->SetDefaultParameter("flat_tree:gem_samples_signal_only", m_gem_samples_signal_only,
                             "Write only gem_sample_data samples above the n-sigma noise "
                             "threshold (~1.2% of them on run 8169). Keeps the signal for "
                             "every event at ~1% of the size; drops the baseline samples "
                             "that pedestal studies need");
    app->SetDefaultParameter("flat_tree:write_gem_clusters", m_io_config.gem_clusters,
                             "Write gem_scluster_* branches (GEM cluster reconstruction)");
    app->SetDefaultParameter("flat_tree:write_gem_peaks", m_io_config.gem_peaks,
                             "Write gem_peak_* branches (GEM plane peaks)");
    app->SetDefaultParameter("flat_tree:write_gem_prerecon", m_io_config.gem_prerecon,
                             "Write srs_prerecon_* branches (decoded per-plane data)");
    app->SetDefaultParameter("flat_tree:write_f125_raw", m_io_config.f125_raw,
                             "Write f125_wraw_* branches (raw FA125 window data)");
    app->SetDefaultParameter("flat_tree:write_f250_raw", m_io_config.f250_raw,
                             "Write f250_wraw_* branches (raw FA250 window data)");
    app->SetDefaultParameter("flat_tree:write_f125_pulse", m_io_config.f125_pulse,
                             "Write f125_pulse_* branches");
    app->SetDefaultParameter("flat_tree:write_f250_pulse", m_io_config.f250_pulse,
                             "Write f250_pulse_* branches");
    app->SetDefaultParameter("flat_tree:write_fpga", m_io_config.fpga,
                             "Write fpga_* branches. Off by default: the 'fpgacon' plugin "
                             "is retired, so these are written empty for every event");

    // Output compression for the events tree. In MT mode every thread's buffers
    // are funneled through TBufferMerger's single writer thread, which compresses
    // on the way to disk - ROOT's default ZLIB is CPU-heavy and becomes the
    // throughput cap for the large raw-SRS payload (~60 kB/event). LZ4 is much
    // cheaper to compress (slightly bigger files) so the writer keeps up.
    // Value is algo*100+level. ZSTD:5 is the default: it compresses better than
    // ROOT's default ZLIB while being several times faster, which matters on both
    // axes here (writer-thread throughput AND output size). Use LZ4 (404) if you
    // are purely throughput-bound and have disk to spare.
    int compression = ROOT::CompressionSettings(ROOT::RCompressionSetting::EAlgorithm::kZSTD, 5);
    app->SetDefaultParameter("flat_tree:compression", compression,
                             "ROOT compression for the events tree "
                             "(algo*100+level; 505=ZSTD:5, 404=LZ4:4, 101=ZLIB:1, 0=none)");

    m_mt_mode = (nthreads_str != "1") || !m_mt_output.empty();

    if (m_mt_mode) {
        std::string histsfile = "output.root";
        app->SetDefaultParameter("histsfile", histsfile,
                                 "Name of root file to be created for plugin histograms/trees");
        if (m_mt_output.empty()) {
            std::string stem = histsfile;
            auto pos = stem.rfind(".root");
            if (pos != std::string::npos) stem = stem.substr(0, pos);
            m_mt_output = stem + "_events.root";
        }
        m_merger = std::make_unique<ROOT::TBufferMerger>(m_mt_output.c_str(), "RECREATE", compression);
        m_log->info("MT writer mode: events tree -> '{}' via TBufferMerger "
                       "(flush every {} fills/thread). Entries are unordered; "
                       "sort by the event_number leaf.", m_mt_output, m_flush_events);
    } else {
        // Legacy single-thread mode: tree lives in the shared hists file
        auto root_file_service = app->GetService<RootFile_service>();
        auto file = root_file_service->GetHistFile();
        file->cd();
        m_main_dir = gDirectory;

        auto ctx = std::make_unique<WriterCtx>(m_io_config);
        ctx->tree = new TTree("events", "jana4ml4fpga_tree_v1");
        ctx->tree->SetDirectory(m_main_dir);
        ctx->io.bindToTree(ctx->tree);
        m_legacy_ctx = ctx.get();
        m_contexts.push_back(std::move(ctx));
        m_log->info("Single-thread writer mode: events tree in shared hists file");
    }

    // Report the enabled groups: output size is dominated by srs_raw and
    // gem_sample_data, so make it obvious which of them this run is paying for.
    m_log->info("Events tree branch groups: srs_raw={} gem_samples={} gem_clusters={} "
                "gem_peaks={} gem_prerecon={} f125_raw={} f250_raw={} f125_pulse={} "
                "f250_pulse={} fpga={} (compression={})",
                m_io_config.srs_raw, m_io_config.gem_samples, m_io_config.gem_clusters,
                m_io_config.gem_peaks, m_io_config.gem_prerecon, m_io_config.f125_raw,
                m_io_config.f250_raw, m_io_config.f125_pulse, m_io_config.f250_pulse,
                m_io_config.fpga, compression);
    if (m_io_config.gem_samples && m_gem_samples_max_events == 0) {
        m_log->warn("gem_sample_data is being written for EVERY event (~76 kB/event, "
                    "~760 GB per 10M events). Set -Pflat_tree:gem_samples_max_events=N "
                    "to keep it only for the first N events.");
    }
    if (m_io_config.srs_raw || m_io_config.gem_prerecon) {
        m_log->info("srs_raw/srs_prerecon are enabled (~4/~7 kB per event). They are "
                    "off by default because the standard analysis does not read them.");
    }

    m_log->info("Initialization is done");
}

//------------------
// GetCtx
//------------------
FlatTreeWriterProcessor::WriterCtx& FlatTreeWriterProcessor::GetCtx() {
    if (!m_mt_mode) return *m_legacy_ctx;

    // One writer context per worker thread (single processor instance assumed -
    // there is exactly one FlatTreeWriterProcessor per application).
    static thread_local WriterCtx* tl_ctx = nullptr;
    if (tl_ctx) return *tl_ctx;

    auto ctx = std::make_unique<WriterCtx>(m_io_config);
    ctx->file = m_merger->GetFile();  // thread-safe
    ctx->tree = new TTree("events", "jana4ml4fpga_tree_v1");
    ctx->tree->SetDirectory(ctx->file.get());
    ctx->io.bindToTree(ctx->tree);

    std::lock_guard<std::mutex> lock(m_ctx_mutex);
    tl_ctx = ctx.get();
    m_contexts.push_back(std::move(ctx));
    m_log->debug("Created writer context #{}", m_contexts.size());
    return *tl_ctx;
}

//------------------
// Process
//------------------
// This function is called every event; in MT mode concurrently from many threads,
// each filling its own tree - no locking on the fill path.
void FlatTreeWriterProcessor::Process(const std::shared_ptr<const JEvent> &event) {

    auto& ctx = GetCtx();
    auto& io = ctx.io;

    m_log->debug("=======================");
    m_log->debug("Event number {}", event->GetEventNumber());

    // Clear writing classes on new event
    io.clear();
    io.m_event_number = event->GetEventNumber();

    // Print what we have in events
    if (m_log->level() <= spdlog::level::debug) {
        m_log->debug("Available data (factories actually)");
        m_log->debug("    {:<30}  {}", "[name]", "[objects count]");
        for (auto factory: event->GetFactorySet()->GetAllFactories()) {
            m_log->debug("    {:<30}  {}", factory->GetObjectName(), factory->GetNumObjects());
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

        // NOTE: the has_* flags record what the EVENT contains and gate the
        // reconstruction blocks below - they must be set even when the
        // corresponding raw branch group is switched off, otherwise disabling
        // e.g. srs_raw would silently disable GEM reconstruction too.
        if (JTypeInfo::demangle<Df125FDCPulse>() == obj_name && obj_num > 0) {
            has_f125_pulse_data = true;
            if (m_io_config.f125_pulse) SaveF125FDCPulse(io, event->Get<Df125FDCPulse>());
        }

        if (JTypeInfo::demangle<Df250PulseData>() == obj_name && obj_num > 0) {
            has_f250_pulse_data = true;
            if (m_io_config.f250_pulse) SaveF250FDCPulse(io, event->Get<Df250PulseData>());
        }

        if (JTypeInfo::demangle<Df125WindowRawData>() == obj_name && obj_num > 0) {
            has_f125_window_raw_data = true;
            if (m_io_config.f125_raw) SaveF125WindowRawData(io, event->Get<Df125WindowRawData>());
        }

        if (JTypeInfo::demangle<Df250WindowRawData>() == obj_name && obj_num > 0) {
            has_f250_window_raw_data = true;
            if (m_io_config.f250_raw) SaveF250WindowRawData(io, event->Get<Df250WindowRawData>());
        }

        if (JTypeInfo::demangle<DGEMSRSWindowRawData>() == obj_name && obj_num > 0) {
            has_srs_window_raw_data = true;
            if (m_io_config.srs_raw) SaveGEMSRSWindowRawData(io, event->Get<DGEMSRSWindowRawData>());
        }
    }

    // GO OVER FACTORY GENERATED DATA
    // MIGRATION (JANA2 2026.x): the old pattern scanned GetAllFactories() for the
    // PlanePeak factory by object name, which modern (JOmniFactory/databundle)
    // factories do not surface the same way - and it also failed to trigger lazy
    // factories reliably. Request the products directly instead; absence of the
    // gemrecon2/fpgacon plugins simply leaves the try-blocks empty.

    // PLUGIN 'gemrecon2' data
    if (has_srs_window_raw_data && (m_io_config.gem_clusters || m_io_config.gem_prerecon ||
                                    m_io_config.gem_peaks || m_io_config.gem_samples)) {
        try {
            if (m_io_config.gem_clusters) {
                SaveGEMSimpleClusters(io, event->Get<ml4fpga::gem::SFclust>());
            }
            if (m_io_config.gem_prerecon) {
                SaveGEMDecodedData(io, event->GetSingle<ml4fpga::gem::PlaneDecodedData>());
            }
            if (m_io_config.gem_peaks) {
                SaveGEMPlanePeak(io, event->Get<ml4fpga::gem::PlanePeak>());
            }
            // Per-sample diagnostics: optionally only for the first N events
            // (see m_gem_samples_max_events). Skipping the Get<> also avoids
            // serializing samples we would immediately throw away.
            if (m_io_config.gem_samples &&
                (m_gem_samples_max_events == 0 ||
                 m_gem_samples_seen.fetch_add(1, std::memory_order_relaxed) < m_gem_samples_max_events)) {
                SaveGEMSampleData(io, event->Get<ml4fpga::gem::SampleData>());
            }
        }
        catch (std::exception &ex) {
            // No GEM reconstruction in this configuration (or it failed) - debug only
            m_log->debug("GEM recon data unavailable: {}", ex.what());
        }
    }

    // PLUGIN 'fpgacon' data (plugin currently not ported - data appears when it is)
    if (has_f125_window_raw_data && m_io_config.fpga) {
        try {
            auto clusters = event->Get<ml4fpga::fpgacon::F125Cluster>();
            SaveFPGAClusters(io, clusters);
            auto hit_track_assocs = event->Get<ml4fpga::fpgacon::FpgaHitsToTrack>();
            SaveFPGAHitsToTracks(io, hit_track_assocs);
            auto track_fits = event->Get<ml4fpga::fpgacon::FpgaTrackFit>();
            SaveFPGATrackFits(io, track_fits);
        }
        catch (std::exception &ex) {
            m_log->debug("FPGA recon data unavailable: {}", ex.what());
        }
    }

    // Fill the tree
    if (m_mt_mode) {
        ctx.tree->Fill();
        if (++ctx.fills_since_flush >= m_flush_events) {
            ctx.file->Write();  // push this thread's buffer to the merger
            ctx.fills_since_flush = 0;
        }
    } else {
        m_main_dir->cd();
        ctx.tree->Fill();
    }

    m_log->debug("Event number {}", event->GetEventNumber());
}

//------------------
// Finish
//------------------
void FlatTreeWriterProcessor::Finish() {
    if (m_mt_mode) {
        std::lock_guard<std::mutex> lock(m_ctx_mutex);
        size_t total = 0;
        for (auto &ctx: m_contexts) {
            total += static_cast<size_t>(ctx->tree->GetEntries());
            ctx->file->Write();  // final flush of this thread's buffer
        }
        m_contexts.clear();      // release TBufferMergerFile handles
        m_merger.reset();        // finalize the merged output file
        m_log->info("MT writer finished: {} entries merged into '{}'", total, m_mt_output);
    } else {
        m_main_dir->cd();
        m_legacy_ctx->tree->Write();
    }
}

uint16_t FlatTreeWriterProcessor::findBestSrsSamle(std::vector<uint16_t> samples) {
    // Examining data, there might be the next number of samples:
    // 1, 3, 4, n
    if (samples.size() == 0) {
        // empty array
        return 0;
    }

    if (samples.size() == 4) {
        // when 4 values are saved, the first is wrongly big, and 3 are to be chosen from
        std::vector<uint16_t> real_samples = {samples[1], samples[2], samples[3]};
        return findBestSrsSamle(real_samples);
    }

    // If we are here, just find the biggest sample
    uint16_t best_sample = samples[0];
    for (auto sample: samples) {
        if (sample > best_sample) best_sample = sample;
    }
    return best_sample;
}

void FlatTreeWriterProcessor::SaveF125FDCPulse(flatio::FlatIoBundle& io, const std::vector<const Df125FDCPulse *>& records) {
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
        io.m_f125_pulse_io.add(save_struct);
    }
}

void FlatTreeWriterProcessor::SaveF250FDCPulse(flatio::FlatIoBundle& io, const std::vector<const Df250PulseData *>& records) {
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
        io.m_f250_pulse_io.add(save_struct);
    }
}

void FlatTreeWriterProcessor::SaveGEMSRSWindowRawData(flatio::FlatIoBundle& io, std::vector<const DGEMSRSWindowRawData *> records) {
    m_log->trace("Writing DGEMSRSWindowRawData data items: {} ", records.size());
    for (auto srs_item: records) {
        flatio::SrsRawRecord srs_save{};
        srs_save.roc = srs_item->rocid;
        srs_save.slot = srs_item->slot;
        srs_save.channel = srs_item->channel;
        srs_save.apv_id = srs_item->apv_id;
        srs_save.channel_apv = ml4fpga::gem::Constants::ApvChannelCorrection(srs_item->channel_apv);

        for (auto sample: srs_item->samples) {
            srs_save.samples.push_back(sample);
        }
        io.m_srs_record_io.add(srs_save);
    }
}

void FlatTreeWriterProcessor::SaveF125WindowRawData(flatio::FlatIoBundle& io, std::vector<const Df125WindowRawData *> records) {
    for (auto record: records) {
        flatio::F125WindowRawRecord f125_wraw_save{};
        f125_wraw_save.roc = record->rocid;
        f125_wraw_save.slot = record->slot;
        f125_wraw_save.channel = record->channel;
        f125_wraw_save.overflow = record->overflow;
        f125_wraw_save.invalid_samples = record->invalid_samples;
        f125_wraw_save.itrigger = record->itrigger;
        f125_wraw_save.samples = record->samples;
        io.m_f125_wraw_io.add(f125_wraw_save);
    }
}

void FlatTreeWriterProcessor::SaveF250WindowRawData(flatio::FlatIoBundle& io, std::vector<const Df250WindowRawData *> records) {
    for (auto record: records) {
        flatio::F250WindowRawRecord f250_wraw_save{};
        f250_wraw_save.roc = record->rocid;
        f250_wraw_save.slot = record->slot;
        f250_wraw_save.channel = record->channel;
        f250_wraw_save.overflow = record->overflow;
        f250_wraw_save.invalid_samples = record->invalid_samples;
        f250_wraw_save.itrigger = record->itrigger;
        f250_wraw_save.samples = record->samples;
        io.m_f250_wraw_io.add(f250_wraw_save);
    }
}

void FlatTreeWriterProcessor::SaveGEMSimpleClusters(flatio::FlatIoBundle& io, std::vector<const ml4fpga::gem::SFclust *> clusters) {
    for (auto cluster: clusters) {
        flatio::GemSimpleCluster cluster_save;
        cluster_save.x = cluster->pos_x;
        cluster_save.y = cluster->pos_y;
        cluster_save.energy = cluster->energy;
        cluster_save.adc = cluster->amplitude;
        io.m_gem_scluster_io.add(cluster_save);
    }
}

void FlatTreeWriterProcessor::SaveGEMDecodedData(flatio::FlatIoBundle& io, const ml4fpga::gem::PlaneDecodedData *data) {
    // Planes may be absent for mappings without these plane names - skip silently
    // (configure with -Pgemrecon:plane_name_x/_y)
    if (data == nullptr
        || !data->plane_data.count(m_gem_plane_x)
        || !data->plane_data.count(m_gem_plane_y)) {
        return;
    }
    const auto& plane_data_x = data->plane_data.at(m_gem_plane_x);
    const auto& plane_data_y = data->plane_data.at(m_gem_plane_y);

    for (size_t time_i = 0; time_i < plane_data_x.data.size(); time_i++) {
        for (size_t adc_i = 0; adc_i < plane_data_x.data[time_i].size(); adc_i++) {
            flatio::SrsPreReconRecord record;
            record.x = plane_data_x.data[time_i][adc_i];
            record.y = plane_data_y.data[time_i][adc_i];
            io.m_srs_prerecon_io.add(record);
        }
    }
}

void FlatTreeWriterProcessor::SaveGEMPlanePeak(flatio::FlatIoBundle& io, const std::vector<const ml4fpga::gem::PlanePeak *> &peaks) {
    for (const auto peak: peaks) {
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
        io.m_gem_peak_io.add(save_peak);
    }
}

void FlatTreeWriterProcessor::SaveGEMSampleData(flatio::FlatIoBundle& io, const std::vector<const ml4fpga::gem::SampleData *> &samples) {
    for (const auto sample: samples) {
        // ~98.8% of samples are baseline noise (measured, run 8169) - skip them
        // when only the signal is wanted. See m_gem_samples_signal_only.
        if (m_gem_samples_signal_only && sample->is_noise) continue;
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
        io.m_gem_sample_data_io.add(sample_save);
    }
}

void FlatTreeWriterProcessor::SaveFPGAClusters(flatio::FlatIoBundle& io, const std::vector<const ml4fpga::fpgacon::F125Cluster *> &clusters) {
    for (const auto cluster: clusters) {
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
        io.m_fpga_f125_cluster_io.add(cluster_save);
    }
}

void FlatTreeWriterProcessor::SaveFPGAHitsToTracks(flatio::FlatIoBundle& io, const std::vector<const ml4fpga::fpgacon::FpgaHitsToTrack *> &ht_assocs) {
    for (const auto hit_track_assoc: ht_assocs) {
        flatio::FpgaHitToTrack ht_save;
        ht_save.hit_index = hit_track_assoc->hit_index;
        ht_save.track_index = hit_track_assoc->track_index;
        io.m_fpga_hits_to_track_io.add(ht_save);
    }
}

void FlatTreeWriterProcessor::SaveFPGATrackFits(flatio::FlatIoBundle& io, const std::vector<const ml4fpga::fpgacon::FpgaTrackFit *> &tfits) {
    for (const auto tfit: tfits) {
        flatio::FpgaTrackFit trk_fit_save;
        trk_fit_save.id = tfit->track_id;
        trk_fit_save.slope = tfit->slope;
        trk_fit_save.intersect = tfit->intersect;
        io.m_fpga_track_fit_io.add(trk_fit_save);
    }
}
