#include "SroFrameUnfolder.h"

#include <JANA/JApplication.h>
#include <JANA/JEvent.h>

#include "SroFrameRef.h"

SroFrameUnfolder::SroFrameUnfolder() {
    SetTypeName(NAME_OF_THIS);
    SetParentLevel(JEventLevel::Timeslice);
    SetChildLevel(JEventLevel::PhysicsEvent);
}

void SroFrameUnfolder::Init() {
    auto* app = GetApplication();
    app->SetDefaultParameter("evio6_file:finder", m_finder_mode, "Frame selection: 'bypass' (every frame -> event) or 'ecal' (ECAL charge threshold)");
    app->SetDefaultParameter("evio6_file:finder_min_ecal_charge", m_min_ecal_charge, "'ecal' finder: min summed ECAL raw ADC charge in a frame");
}

bool SroFrameUnfolder::FramePassesFinder(const sro::SroBlockData& block, uint32_t frame_index) const {
    if (m_finder_mode != "ecal") {
        return true; // bypass
    }
    const sro::FrameInfo& frame = block.frames[frame_index];
    uint64_t ecal_charge = 0;
    for (uint32_t hit_i = frame.first_fadc_hit; hit_i < frame.first_fadc_hit + frame.fadc_hit_count; hit_i++) {
        if (block.fadc_hits[hit_i].detector == static_cast<int8_t>(sro::Detector::Ecal)) {
            ecal_charge += block.fadc_hits[hit_i].charge;
        }
    }
    return ecal_charge >= m_min_ecal_charge;
}

JEventUnfolder::Result SroFrameUnfolder::Unfold(const JEvent& parent, JEvent& child, int child_idx) {
    const auto* block = parent.GetSingle<sro::SroBlockData>();

    if (child_idx == 0) {
        m_next_frame = 0; // child_idx resets to 0 exactly when JANA moves to a new parent
    }

    // Advance the cursor to the next frame that passes the finder. In bypass
    // mode this loop body runs exactly once per frame.
    while (m_next_frame < block->frames.size()) {
        uint32_t frame_index = m_next_frame++;
        m_frames_seen++;
        if (!FramePassesFinder(*block, frame_index)) {
            continue;
        }
        m_frames_emitted++;
        child.SetEventNumber(block->frames[frame_index].frame_number);
        child.Insert(new SroFrameRef{block, frame_index});
        bool parent_exhausted = (m_next_frame >= block->frames.size());
        return parent_exhausted ? Result::NextChildNextParent : Result::NextChildKeepParent;
    }
    return Result::KeepChildNextParent; // no (further) frame in this block passed the finder
}

void SroFrameUnfolder::Finish() {
    LOG_INFO(GetLogger()) << "SroFrameUnfolder: emitted " << m_frames_emitted << " of " << m_frames_seen
                          << " frames (finder=" << m_finder_mode << ")" << LOG_END;
}
