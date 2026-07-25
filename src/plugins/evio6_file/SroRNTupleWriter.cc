#include "SroRNTupleWriter.h"

#include <JANA/JApplication.h>
#include <JANA/JEvent.h>

#include <ROOT/RNTupleModel.hxx>
#include <ROOT/RNTupleWriter.hxx>
#include <TFile.h>

#include "SroFrameRef.h"

struct SroRNTupleWriter::Fields {
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
};

SroRNTupleWriter::SroRNTupleWriter() {
    SetTypeName(NAME_OF_THIS);
    SetCallbackStyle(CallbackStyle::ExpertMode);
}

void SroRNTupleWriter::Init() {
    GetApplication()->SetDefaultParameter("evio6_file:output_file", m_output_path, "Output ROOT file with frames/fadc_hits/dcrb_hits RNTuples");

    m_file.reset(TFile::Open(m_output_path.c_str(), "RECREATE"));
    if (m_file == nullptr || m_file->IsZombie()) {
        throw JException("SroRNTupleWriter: cannot create output file %s", m_output_path.c_str());
    }
    m_fields = std::make_unique<Fields>();

    auto frames_model = ROOT::RNTupleModel::Create();
    m_fields->frame_number = frames_model->MakeField<std::uint32_t>("frame_number");
    m_fields->timestamp = frames_model->MakeField<std::uint64_t>("timestamp");
    m_fields->block_number = frames_model->MakeField<std::uint32_t>("block_number");
    m_fields->n_fadc_hits = frames_model->MakeField<std::uint32_t>("n_fadc_hits");
    m_fields->n_dcrb_hits = frames_model->MakeField<std::uint32_t>("n_dcrb_hits");
    m_frames_writer = ROOT::RNTupleWriter::Append(std::move(frames_model), "frames", *m_file);

    auto fadc_model = ROOT::RNTupleModel::Create();
    m_fields->fadc_frame_number = fadc_model->MakeField<std::uint32_t>("frame_number");
    m_fields->fadc_rocid = fadc_model->MakeField<std::uint16_t>("rocid");
    m_fields->fadc_slot = fadc_model->MakeField<std::uint8_t>("slot");
    m_fields->fadc_channel = fadc_model->MakeField<std::uint8_t>("channel");
    m_fields->fadc_charge = fadc_model->MakeField<std::uint16_t>("charge");
    m_fields->fadc_time = fadc_model->MakeField<std::uint16_t>("time_ticks");
    m_fields->fadc_detector = fadc_model->MakeField<std::int8_t>("detector");
    m_fields->fadc_sector = fadc_model->MakeField<std::int8_t>("sector");
    m_fields->fadc_io = fadc_model->MakeField<std::int8_t>("io");
    m_fields->fadc_view = fadc_model->MakeField<std::int8_t>("view");
    m_fields->fadc_strip = fadc_model->MakeField<std::int16_t>("strip");
    m_fadc_writer = ROOT::RNTupleWriter::Append(std::move(fadc_model), "fadc_hits", *m_file);

    auto dcrb_model = ROOT::RNTupleModel::Create();
    m_fields->dcrb_frame_number = dcrb_model->MakeField<std::uint32_t>("frame_number");
    m_fields->dcrb_rocid = dcrb_model->MakeField<std::uint16_t>("rocid");
    m_fields->dcrb_slot = dcrb_model->MakeField<std::uint8_t>("slot");
    m_fields->dcrb_channel = dcrb_model->MakeField<std::uint8_t>("channel");
    m_fields->dcrb_time = dcrb_model->MakeField<std::uint16_t>("time_ticks");
    m_fields->dcrb_sector = dcrb_model->MakeField<std::int8_t>("sector");
    m_fields->dcrb_region = dcrb_model->MakeField<std::int8_t>("region");
    m_fields->dcrb_superlayer = dcrb_model->MakeField<std::int8_t>("superlayer");
    m_dcrb_writer = ROOT::RNTupleWriter::Append(std::move(dcrb_model), "dcrb_hits", *m_file);
}

void SroRNTupleWriter::ProcessSequential(const JEvent& event) {
    const auto* ref = event.GetSingle<SroFrameRef>();
    const sro::SroBlockData& block = *ref->block;
    const sro::FrameInfo& frame = ref->Frame();

    std::lock_guard<std::mutex> lock(m_mutex);

    *m_fields->frame_number = frame.frame_number;
    *m_fields->timestamp = frame.timestamp;
    *m_fields->block_number = block.block_number;
    *m_fields->n_fadc_hits = frame.fadc_hit_count;
    *m_fields->n_dcrb_hits = frame.dcrb_hit_count;
    m_frames_writer->Fill();

    for (uint32_t hit_i = frame.first_fadc_hit; hit_i < frame.first_fadc_hit + frame.fadc_hit_count; hit_i++) {
        const sro::FadcHit& hit = block.fadc_hits[hit_i];
        *m_fields->fadc_frame_number = frame.frame_number;
        *m_fields->fadc_rocid = hit.rocid;
        *m_fields->fadc_slot = hit.slot;
        *m_fields->fadc_channel = hit.channel;
        *m_fields->fadc_charge = hit.charge;
        *m_fields->fadc_time = hit.time_ticks;
        *m_fields->fadc_detector = hit.detector;
        *m_fields->fadc_sector = hit.sector;
        *m_fields->fadc_io = hit.io;
        *m_fields->fadc_view = hit.view;
        *m_fields->fadc_strip = hit.strip;
        m_fadc_writer->Fill();
    }

    for (uint32_t hit_i = frame.first_dcrb_hit; hit_i < frame.first_dcrb_hit + frame.dcrb_hit_count; hit_i++) {
        const sro::DcrbHit& hit = block.dcrb_hits[hit_i];
        *m_fields->dcrb_frame_number = frame.frame_number;
        *m_fields->dcrb_rocid = hit.rocid;
        *m_fields->dcrb_slot = hit.slot;
        *m_fields->dcrb_channel = hit.channel;
        *m_fields->dcrb_time = hit.time_ticks;
        *m_fields->dcrb_sector = hit.sector;
        *m_fields->dcrb_region = hit.region;
        *m_fields->dcrb_superlayer = hit.superlayer;
        m_dcrb_writer->Fill();
    }

    m_frames_written++;
    m_fadc_written += frame.fadc_hit_count;
    m_dcrb_written += frame.dcrb_hit_count;
}

void SroRNTupleWriter::Finish() {
    // Writers flush and commit on destruction; they must go before the TFile closes.
    m_frames_writer.reset();
    m_fadc_writer.reset();
    m_dcrb_writer.reset();
    if (m_file != nullptr) {
        m_file->Close();
        m_file.reset();
    }
    LOG_INFO(GetLogger()) << "SroRNTupleWriter: wrote " << m_frames_written << " frames, "
                          << m_fadc_written << " fadc_hits, " << m_dcrb_written << " dcrb_hits to " << m_output_path << LOG_END;
}
