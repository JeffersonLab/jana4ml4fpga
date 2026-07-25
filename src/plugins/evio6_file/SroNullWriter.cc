#include "SroNullWriter.h"

#include <JANA/JEvent.h>

#include "SroFrameRef.h"

SroNullWriter::SroNullWriter() {
    SetTypeName(NAME_OF_THIS);
    SetCallbackStyle(CallbackStyle::ExpertMode);
}

void SroNullWriter::ProcessParallel(const JEvent& event) {
    auto* ref = const_cast<SroFrameRef*>(event.GetSingle<SroFrameRef>());
    const sro::FrameInfo& frame = ref->Frame();
    sro::ParseStats decode_stats;
    ref->EnsureDecoded(decode_stats);

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

    m_checksum.fetch_xor(sum, std::memory_order_relaxed);
    m_frames_seen.fetch_add(1, std::memory_order_relaxed);
    m_fadc_seen.fetch_add(ref->FadcCount(), std::memory_order_relaxed);
    m_dcrb_seen.fetch_add(ref->DcrbCount(), std::memory_order_relaxed);
}

void SroNullWriter::Finish() {
    // Same log format as SroRNTupleWriter so record_perf.py parses either writer.
    LOG_INFO(GetLogger()) << "SroNullWriter: wrote " << m_frames_seen.load() << " frames, "
                          << m_fadc_seen.load() << " fadc_hits, " << m_dcrb_seen.load()
                          << " dcrb_hits to /dev/null (checksum=" << m_checksum.load() << ")" << LOG_END;
}
