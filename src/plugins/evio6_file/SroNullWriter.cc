#include "SroNullWriter.h"

#include <JANA/JEvent.h>

#include "SroFrameRef.h"

SroNullWriter::SroNullWriter() {
    SetTypeName(NAME_OF_THIS);
    SetCallbackStyle(CallbackStyle::ExpertMode);
}

void SroNullWriter::ProcessSequential(const JEvent& event) {
    const auto* ref = event.GetSingle<SroFrameRef>();
    const sro::FrameInfo& frame = ref->Frame();

    std::lock_guard<std::mutex> lock(m_mutex);

    uint64_t sum = frame.frame_number + frame.timestamp;
    const sro::FadcHit* fadc_hits = ref->FadcHits();
    for (uint32_t hit_i = 0; hit_i < ref->FadcCount(); hit_i++) {
        const sro::FadcHit& hit = fadc_hits[hit_i];
        sum += hit.charge + hit.time_ticks + hit.rocid + hit.slot + hit.channel
             + hit.detector + hit.sector + hit.io + hit.view + hit.strip;
    }
    const sro::DcrbHit* dcrb_hits = ref->DcrbHits();
    for (uint32_t hit_i = 0; hit_i < ref->DcrbCount(); hit_i++) {
        const sro::DcrbHit& hit = dcrb_hits[hit_i];
        sum += hit.channel + hit.time_ticks + hit.rocid + hit.slot
             + hit.sector + hit.region + hit.superlayer;
    }

    m_checksum ^= sum;
    m_frames_seen++;
    m_fadc_seen += ref->FadcCount();
    m_dcrb_seen += ref->DcrbCount();
}

void SroNullWriter::Finish() {
    // Same log format as SroRNTupleWriter so record_perf.py parses either writer.
    LOG_INFO(GetLogger()) << "SroNullWriter: wrote " << m_frames_seen << " frames, "
                          << m_fadc_seen << " fadc_hits, " << m_dcrb_seen
                          << " dcrb_hits to /dev/null (checksum=" << m_checksum << ")" << LOG_END;
}
