#include "SroNullWriter.h"

#include <JANA/JEvent.h>

#include "SroFrameRef.h"

SroNullWriter::SroNullWriter() {
    SetTypeName(NAME_OF_THIS);
    SetCallbackStyle(CallbackStyle::ExpertMode);
}

void SroNullWriter::ProcessSequential(const JEvent& event) {
    const auto* ref = event.GetSingle<SroFrameRef>();
    const sro::SroBlockData& block = *ref->block;
    const sro::FrameInfo& frame = ref->Frame();

    std::lock_guard<std::mutex> lock(m_mutex);

    uint64_t sum = frame.frame_number + frame.timestamp;
    for (uint32_t hit_i = frame.first_fadc_hit; hit_i < frame.first_fadc_hit + frame.fadc_hit_count; hit_i++) {
        const sro::FadcHit& hit = block.fadc_hits[hit_i];
        sum += hit.charge + hit.time_ticks + hit.rocid + hit.slot + hit.channel
             + hit.detector + hit.sector + hit.io + hit.view + hit.strip;
    }
    for (uint32_t hit_i = frame.first_dcrb_hit; hit_i < frame.first_dcrb_hit + frame.dcrb_hit_count; hit_i++) {
        const sro::DcrbHit& hit = block.dcrb_hits[hit_i];
        sum += hit.channel + hit.time_ticks + hit.rocid + hit.slot
             + hit.sector + hit.region + hit.superlayer;
    }

    m_checksum ^= sum;
    m_frames_seen++;
    m_fadc_seen += frame.fadc_hit_count;
    m_dcrb_seen += frame.dcrb_hit_count;
}

void SroNullWriter::Finish() {
    // Same log format as SroRNTupleWriter so record_perf.py parses either writer.
    LOG_INFO(GetLogger()) << "SroNullWriter: wrote " << m_frames_seen << " frames, "
                          << m_fadc_seen << " fadc_hits, " << m_dcrb_seen
                          << " dcrb_hits to /dev/null (checksum=" << m_checksum << ")" << LOG_END;
}
