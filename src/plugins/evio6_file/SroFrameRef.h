// Child-event payload: points one PhysicsEvent at one frame of the parent
// timeslice's parsed block. Holds a reference, not copies - hit slices live in
// the parent's SroBlockData (JANA keeps the parent alive until all its children
// finish, so the pointer is safe inside processor callbacks).

#pragma once

#include <cstdint>

#include <libraries/evio_sro_parser/SroData.h>

struct SroFrameRef {
    const sro::SroBlockData* block = nullptr;
    uint32_t frame_index = 0;

    const sro::FrameInfo& Frame() const { return block->frames[frame_index]; }
};
