// Unfolds one timeslice (evio block) into PhysicsEvents (frames).
//
// Modes (parameter evio6_file:finder):
//   "bypass" (default) - every frame becomes an event; nothing is dropped.
//   "ecal"             - only frames whose summed ECAL charge passes
//                        evio6_file:finder_min_ecal_charge become events.
//                        Frame-granular selection only: sub-frame time windows
//                        are Phase II (space/notes/DECISIONS.md #5).

#pragma once

#include <cstdint>

#include <JANA/JEventUnfolder.h>

#include <libraries/evio_sro_parser/SroData.h>

class SroFrameUnfolder : public JEventUnfolder {
public:
    SroFrameUnfolder();

    void Init() override;
    Result Unfold(const JEvent& parent, JEvent& child, int child_idx) override;
    void Finish() override;

private:
    bool FramePassesFinder(const sro::SroBlockData& block, uint32_t frame_index) const;

    std::string m_finder_mode = "bypass";
    uint32_t m_min_ecal_charge = 2000; // summed raw ADC per frame; derive from data before trusting
    uint32_t m_next_frame = 0;         // cursor within current parent; child_idx==0 marks a new parent
    uint64_t m_frames_seen = 0;
    uint64_t m_frames_emitted = 0;
};
