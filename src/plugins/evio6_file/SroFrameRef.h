// Child-event payload: points one PhysicsEvent at one frame of the parent
// timeslice's parsed block. Under eager parsing it holds only a reference - hit
// slices live in the parent's SroBlockData (JANA keeps the parent alive until
// all its children finish, so the pointer is safe inside processor callbacks).
// Under lazy parsing the parent holds only ECAL hits, so the unfolder decodes
// the selected frame's full hit set into the vectors here; the span accessors
// hide which mode produced the hits.

#pragma once

#include <cstdint>
#include <vector>

#include <libraries/evio_sro_parser/SroData.h>

struct SroFrameRef {
    const sro::SroBlockData* block = nullptr;
    uint32_t frame_index = 0;
    std::vector<sro::FadcHit> frame_fadc;  ///< lazy mode only: all FADC hits of the frame
    std::vector<sro::DcrbHit> frame_dcrb;  ///< lazy mode only: all DCRB hits of the frame

    const sro::FrameInfo& Frame() const { return block->frames[frame_index]; }

    const sro::FadcHit* FadcHits() const {
        return block->lazy ? frame_fadc.data() : block->fadc_hits.data() + Frame().first_fadc_hit;
    }
    uint32_t FadcCount() const {
        return block->lazy ? static_cast<uint32_t>(frame_fadc.size()) : Frame().fadc_hit_count;
    }
    const sro::DcrbHit* DcrbHits() const {
        return block->lazy ? frame_dcrb.data() : block->dcrb_hits.data() + Frame().first_dcrb_hit;
    }
    uint32_t DcrbCount() const {
        return block->lazy ? static_cast<uint32_t>(frame_dcrb.size()) : Frame().dcrb_hit_count;
    }
};
