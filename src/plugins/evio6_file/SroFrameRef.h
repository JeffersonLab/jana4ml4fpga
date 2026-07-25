// Child-event payload: points one PhysicsEvent at one frame of the parent
// timeslice's parsed block. Under eager parsing it holds only a reference - hit
// slices live in the parent's SroBlockData (JANA keeps the parent alive until
// all its children finish, so the pointer is safe inside processor callbacks).
// Under lazy parsing the parent holds only ECAL hits; the consuming processor
// calls EnsureDecoded() (on the parallel PhysicsEventMap2 arrow) to materialize
// the frame's full hit set here. The span accessors hide which mode applied.

#pragma once

#include <cstdint>
#include <vector>

#include <libraries/evio_sro_parser/SroData.h>
#include <libraries/evio_sro_parser/SroFrameSetParser.h>

struct SroFrameRef {
    const sro::SroBlockData* block = nullptr;
    uint32_t frame_index = 0;
    bool decoded = false;                  ///< lazy mode: full hit set materialized below
    std::vector<sro::FadcHit> frame_fadc;  ///< lazy mode only: all FADC hits of the frame
    std::vector<sro::DcrbHit> frame_dcrb;  ///< lazy mode only: all DCRB hits of the frame

    const sro::FrameInfo& Frame() const { return block->frames[frame_index]; }

    /// Lazy mode: copy the frame's ECAL slice and decode its deferred ROC banks.
    /// Each child event has a single consumer, so no synchronization is needed.
    void EnsureDecoded(sro::ParseStats& stats) {
        if (!block->lazy || decoded) {
            return;
        }
        const sro::FrameInfo& frame = Frame();
        frame_fadc.assign(block->fadc_hits.begin() + frame.first_fadc_hit,
                          block->fadc_hits.begin() + frame.first_fadc_hit + frame.fadc_hit_count);
        sro::DecodeDeferredFrame(*block, frame_index, frame_fadc, frame_dcrb, stats);
        decoded = true;
    }

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
