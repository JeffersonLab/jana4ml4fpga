#include "SroRNTupleWriter.h"

#include <JANA/JApplication.h>
#include <JANA/JEvent.h>

#include <ROOT/REntry.hxx>
#include <ROOT/RNTupleFillContext.hxx>
#include <ROOT/RNTupleFillStatus.hxx>
#include <ROOT/RNTupleModel.hxx>
#include <ROOT/RNTupleParallelWriter.hxx>
#include <ROOT/RNTupleWriteOptions.hxx>
#include <TFile.h>

#include "SroFrameRef.h"

// One thread's private fill state: a fill context + entry per table, plus the
// typed pointers bound into each entry. Created lazily on the thread's first
// frame; owned by the writer's registry so Finish can flush them all.
struct SroRNTupleWriter::ThreadContexts {
    std::shared_ptr<ROOT::RNTupleFillContext> frames_ctx;
    std::shared_ptr<ROOT::RNTupleFillContext> fadc_ctx;
    std::shared_ptr<ROOT::RNTupleFillContext> dcrb_ctx;
    std::unique_ptr<ROOT::REntry> frames_entry;
    std::unique_ptr<ROOT::REntry> fadc_entry;
    std::unique_ptr<ROOT::REntry> dcrb_entry;

    // frames table
    std::shared_ptr<std::uint32_t> frame_number;
    std::shared_ptr<std::uint64_t> timestamp;
    std::shared_ptr<std::uint32_t> block_number;
    std::shared_ptr<std::uint32_t> n_fadc_hits;
    std::shared_ptr<std::uint32_t> n_dcrb_hits;
    // fadc_hits table
    std::shared_ptr<std::uint32_t> fadc_frame_number;
    std::shared_ptr<std::uint16_t> fadc_rocid;
    std::shared_ptr<std::uint8_t> fadc_slot;
    std::shared_ptr<std::uint8_t> fadc_channel;
    std::shared_ptr<std::uint16_t> fadc_charge;
    std::shared_ptr<std::uint16_t> fadc_time;
    std::shared_ptr<std::int8_t> fadc_detector;
    std::shared_ptr<std::int8_t> fadc_sector;
    std::shared_ptr<std::int8_t> fadc_io;
    std::shared_ptr<std::int8_t> fadc_view;
    std::shared_ptr<std::int16_t> fadc_strip;
    // dcrb_hits table
    std::shared_ptr<std::uint32_t> dcrb_frame_number;
    std::shared_ptr<std::uint16_t> dcrb_rocid;
    std::shared_ptr<std::uint8_t> dcrb_slot;
    std::shared_ptr<std::uint8_t> dcrb_channel;
    std::shared_ptr<std::uint16_t> dcrb_time;
    std::shared_ptr<std::int8_t> dcrb_sector;
    std::shared_ptr<std::int8_t> dcrb_region;
    std::shared_ptr<std::int8_t> dcrb_superlayer;

    // Fill one entry; when the cluster is due, compress outside the file lock
    // and commit under it (the shared TFile is not thread-safe).
    void Fill(ROOT::RNTupleFillContext& context, ROOT::REntry& entry, std::mutex& file_mutex) {
        ROOT::RNTupleFillStatus status;
        context.FillNoFlush(entry, status);
        if (status.ShouldFlushCluster()) {
            context.FlushColumns();
            std::lock_guard<std::mutex> lock(file_mutex);
            context.FlushCluster();
        }
    }
};

SroRNTupleWriter::SroRNTupleWriter() {
    SetTypeName(NAME_OF_THIS);
    SetCallbackStyle(CallbackStyle::ExpertMode);
}

void SroRNTupleWriter::Init() {
    GetApplication()->SetDefaultParameter("evio6_file:output_file", m_output_path, "Output ROOT file with frames/fadc_hits/dcrb_hits RNTuples");
    GetApplication()->SetDefaultParameter("evio6_file:compression", m_compression, "ROOT compression setting for the output RNTuples (-1 = ROOT default, 0 = none, 501 = zstd-1, 505 = zstd-5)");

    m_file.reset(TFile::Open(m_output_path.c_str(), "RECREATE"));
    if (m_file == nullptr || m_file->IsZombie()) {
        throw JException("SroRNTupleWriter: cannot create output file %s", m_output_path.c_str());
    }

    ROOT::RNTupleWriteOptions write_options;
    if (m_compression >= 0) {
        write_options.SetCompression(static_cast<std::uint32_t>(m_compression));
    }

    auto frames_model = ROOT::RNTupleModel::Create();
    frames_model->MakeField<std::uint32_t>("frame_number");
    frames_model->MakeField<std::uint64_t>("timestamp");
    frames_model->MakeField<std::uint32_t>("block_number");
    frames_model->MakeField<std::uint32_t>("n_fadc_hits");
    frames_model->MakeField<std::uint32_t>("n_dcrb_hits");
    m_frames_writer = ROOT::RNTupleParallelWriter::Append(std::move(frames_model), "frames", *m_file, write_options);

    auto fadc_model = ROOT::RNTupleModel::Create();
    fadc_model->MakeField<std::uint32_t>("frame_number");
    fadc_model->MakeField<std::uint16_t>("rocid");
    fadc_model->MakeField<std::uint8_t>("slot");
    fadc_model->MakeField<std::uint8_t>("channel");
    fadc_model->MakeField<std::uint16_t>("charge");
    fadc_model->MakeField<std::uint16_t>("time_ticks");
    fadc_model->MakeField<std::int8_t>("detector");
    fadc_model->MakeField<std::int8_t>("sector");
    fadc_model->MakeField<std::int8_t>("io");
    fadc_model->MakeField<std::int8_t>("view");
    fadc_model->MakeField<std::int16_t>("strip");
    m_fadc_writer = ROOT::RNTupleParallelWriter::Append(std::move(fadc_model), "fadc_hits", *m_file, write_options);

    auto dcrb_model = ROOT::RNTupleModel::Create();
    dcrb_model->MakeField<std::uint32_t>("frame_number");
    dcrb_model->MakeField<std::uint16_t>("rocid");
    dcrb_model->MakeField<std::uint8_t>("slot");
    dcrb_model->MakeField<std::uint8_t>("channel");
    dcrb_model->MakeField<std::uint16_t>("time_ticks");
    dcrb_model->MakeField<std::int8_t>("sector");
    dcrb_model->MakeField<std::int8_t>("region");
    dcrb_model->MakeField<std::int8_t>("superlayer");
    m_dcrb_writer = ROOT::RNTupleParallelWriter::Append(std::move(dcrb_model), "dcrb_hits", *m_file, write_options);
}

SroRNTupleWriter::ThreadContexts& SroRNTupleWriter::GetThreadContexts() {
    static thread_local ThreadContexts* cached = nullptr; // one writer instance per process
    if (cached != nullptr) {
        return *cached;
    }
    auto contexts = std::make_unique<ThreadContexts>();
    {
        std::lock_guard<std::mutex> lock(m_file_mutex);
        contexts->frames_ctx = m_frames_writer->CreateFillContext();
        contexts->fadc_ctx = m_fadc_writer->CreateFillContext();
        contexts->dcrb_ctx = m_dcrb_writer->CreateFillContext();
    }
    contexts->frames_entry = contexts->frames_ctx->CreateEntry();
    contexts->fadc_entry = contexts->fadc_ctx->CreateEntry();
    contexts->dcrb_entry = contexts->dcrb_ctx->CreateEntry();

    contexts->frame_number = contexts->frames_entry->GetPtr<std::uint32_t>("frame_number");
    contexts->timestamp = contexts->frames_entry->GetPtr<std::uint64_t>("timestamp");
    contexts->block_number = contexts->frames_entry->GetPtr<std::uint32_t>("block_number");
    contexts->n_fadc_hits = contexts->frames_entry->GetPtr<std::uint32_t>("n_fadc_hits");
    contexts->n_dcrb_hits = contexts->frames_entry->GetPtr<std::uint32_t>("n_dcrb_hits");

    contexts->fadc_frame_number = contexts->fadc_entry->GetPtr<std::uint32_t>("frame_number");
    contexts->fadc_rocid = contexts->fadc_entry->GetPtr<std::uint16_t>("rocid");
    contexts->fadc_slot = contexts->fadc_entry->GetPtr<std::uint8_t>("slot");
    contexts->fadc_channel = contexts->fadc_entry->GetPtr<std::uint8_t>("channel");
    contexts->fadc_charge = contexts->fadc_entry->GetPtr<std::uint16_t>("charge");
    contexts->fadc_time = contexts->fadc_entry->GetPtr<std::uint16_t>("time_ticks");
    contexts->fadc_detector = contexts->fadc_entry->GetPtr<std::int8_t>("detector");
    contexts->fadc_sector = contexts->fadc_entry->GetPtr<std::int8_t>("sector");
    contexts->fadc_io = contexts->fadc_entry->GetPtr<std::int8_t>("io");
    contexts->fadc_view = contexts->fadc_entry->GetPtr<std::int8_t>("view");
    contexts->fadc_strip = contexts->fadc_entry->GetPtr<std::int16_t>("strip");

    contexts->dcrb_frame_number = contexts->dcrb_entry->GetPtr<std::uint32_t>("frame_number");
    contexts->dcrb_rocid = contexts->dcrb_entry->GetPtr<std::uint16_t>("rocid");
    contexts->dcrb_slot = contexts->dcrb_entry->GetPtr<std::uint8_t>("slot");
    contexts->dcrb_channel = contexts->dcrb_entry->GetPtr<std::uint8_t>("channel");
    contexts->dcrb_time = contexts->dcrb_entry->GetPtr<std::uint16_t>("time_ticks");
    contexts->dcrb_sector = contexts->dcrb_entry->GetPtr<std::int8_t>("sector");
    contexts->dcrb_region = contexts->dcrb_entry->GetPtr<std::int8_t>("region");
    contexts->dcrb_superlayer = contexts->dcrb_entry->GetPtr<std::int8_t>("superlayer");

    std::lock_guard<std::mutex> lock(m_registry_mutex);
    m_thread_contexts.push_back(std::move(contexts));
    cached = m_thread_contexts.back().get();
    return *cached;
}

void SroRNTupleWriter::ProcessParallel(const JEvent& event) {
    auto* ref = const_cast<SroFrameRef*>(event.GetSingle<SroFrameRef>());
    const sro::SroBlockData& block = *ref->block;
    const sro::FrameInfo& frame = ref->Frame();
    sro::ParseStats decode_stats;
    ref->EnsureDecoded(decode_stats);
    if (decode_stats.structure_errors > 0) {
        std::lock_guard<std::mutex> lock(m_decode_stats_mutex);
        m_decode_stats.Add(decode_stats);
    }
    ThreadContexts& ctx = GetThreadContexts();

    *ctx.frame_number = frame.frame_number;
    *ctx.timestamp = frame.timestamp;
    *ctx.block_number = block.block_number;
    *ctx.n_fadc_hits = ref->FadcCount();
    *ctx.n_dcrb_hits = ref->DcrbCount();
    ctx.Fill(*ctx.frames_ctx, *ctx.frames_entry, m_file_mutex);

    const sro::FadcHit* fadc_hits = ref->FadcHits();
    for (uint32_t hit_i = 0; hit_i < ref->FadcCount(); hit_i++) {
        const sro::FadcHit& hit = fadc_hits[hit_i];
        *ctx.fadc_frame_number = frame.frame_number;
        *ctx.fadc_rocid = hit.rocid;
        *ctx.fadc_slot = hit.slot;
        *ctx.fadc_channel = hit.channel;
        *ctx.fadc_charge = hit.charge;
        *ctx.fadc_time = hit.time_ticks;
        *ctx.fadc_detector = hit.detector;
        *ctx.fadc_sector = hit.sector;
        *ctx.fadc_io = hit.io;
        *ctx.fadc_view = hit.view;
        *ctx.fadc_strip = hit.strip;
        ctx.Fill(*ctx.fadc_ctx, *ctx.fadc_entry, m_file_mutex);
    }

    const sro::DcrbHit* dcrb_hits = ref->DcrbHits();
    for (uint32_t hit_i = 0; hit_i < ref->DcrbCount(); hit_i++) {
        const sro::DcrbHit& hit = dcrb_hits[hit_i];
        *ctx.dcrb_frame_number = frame.frame_number;
        *ctx.dcrb_rocid = hit.rocid;
        *ctx.dcrb_slot = hit.slot;
        *ctx.dcrb_channel = hit.channel;
        *ctx.dcrb_time = hit.time_ticks;
        *ctx.dcrb_sector = hit.sector;
        *ctx.dcrb_region = hit.region;
        *ctx.dcrb_superlayer = hit.superlayer;
        ctx.Fill(*ctx.dcrb_ctx, *ctx.dcrb_entry, m_file_mutex);
    }

    m_frames_written.fetch_add(1, std::memory_order_relaxed);
    m_fadc_written.fetch_add(ref->FadcCount(), std::memory_order_relaxed);
    m_dcrb_written.fetch_add(ref->DcrbCount(), std::memory_order_relaxed);
}

void SroRNTupleWriter::Finish() {
    // Destruction order commits everything: fill contexts flush their last
    // clusters (Finish runs single-threaded, so no lock contention), then the
    // parallel writers commit datasets, then the TFile closes.
    m_thread_contexts.clear();
    m_frames_writer.reset();
    m_fadc_writer.reset();
    m_dcrb_writer.reset();
    if (m_file != nullptr) {
        m_file->Close();
        m_file.reset();
    }
    if (m_decode_stats.structure_errors > 0) {
        LOG_WARN(GetLogger()) << "SroRNTupleWriter: deferred decode structure errors: " << m_decode_stats.ToString() << LOG_END;
    }
    LOG_INFO(GetLogger()) << "SroRNTupleWriter: wrote " << m_frames_written.load() << " frames, "
                          << m_fadc_written.load() << " fadc_hits, " << m_dcrb_written.load() << " dcrb_hits to " << m_output_path << LOG_END;
}
