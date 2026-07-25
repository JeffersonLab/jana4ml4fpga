// Unfolds one timeslice (evio block) into PhysicsEvents (frames).
//
// Modes (parameter evio6_file:finder):
//   "bypass" (default)  - every frame becomes an event; nothing is dropped.
//   "coincidence"       - the validated event finder (space/notes/DECISIONS.md #22-23):
//                         a frame is kept when any finder_bin_ticks time bin holds
//                         >= finder_min_hits_in_bin ECAL hits, counting only
//                         channels NOT listed in the hot-channel CSV. Selects
//                         ~4% of frames on sro_000791 (cosmic events).
//   "ecal"              - legacy frame-summed-charge cut (finder_min_ecal_charge).
//                         Kept for comparison; measured to select noise
//                         fluctuations, not events (DECISIONS.md #19).

#pragma once

#include <cstdint>
#include <unordered_set>

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
    void LoadHotChannels(const std::string& csv_path);
    bool IsHot(uint16_t rocid, uint8_t slot, uint8_t channel) const {
        return m_hot_channels.count((static_cast<uint32_t>(rocid) << 16) | (static_cast<uint32_t>(slot) << 8) | channel) != 0;
    }

    std::string m_finder_mode = "bypass";
    // coincidence finder (values validated on sro_000791, DECISIONS.md #23)
    uint32_t m_bin_ticks = 8;          // 8 x 4 ns = 32 ns coincidence window
    uint32_t m_min_hits_in_bin = 6;    // clean ECAL hits in one bin => event
    std::string m_hot_csv;             // channels excluded from the coincidence count
    std::unordered_set<uint32_t> m_hot_channels;
    // legacy 'ecal' mode
    uint32_t m_min_ecal_charge = 25000;

    uint32_t m_next_frame = 0;         // cursor within current parent; child_idx==0 marks a new parent
    uint64_t m_frames_seen = 0;
    uint64_t m_frames_emitted = 0;
};
