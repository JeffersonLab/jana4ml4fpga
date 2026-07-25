#include "SroFrameUnfolder.h"

#include <fstream>
#include <sstream>
#include <vector>

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
    app->SetDefaultParameter("evio6_file:finder", m_finder_mode, "Frame selection: 'bypass', 'coincidence' (time-bin ECAL coincidence, the validated finder) or 'ecal' (legacy charge sum)");
    app->SetDefaultParameter("evio6_file:finder_bin_ticks", m_bin_ticks, "coincidence finder: bin width in 4 ns FADC ticks (8 = 32 ns)");
    app->SetDefaultParameter("evio6_file:finder_min_hits_in_bin", m_min_hits_in_bin, "coincidence finder: clean ECAL hits in one bin that make a frame an event");
    app->SetDefaultParameter("evio6_file:finder_hot_channels", m_hot_csv, "coincidence finder: CSV (rocid,slot,channel,...) of always-on channels to ignore");
    app->SetDefaultParameter("evio6_file:finder_min_ecal_charge", m_min_ecal_charge, "'ecal' finder: min summed ECAL raw ADC charge in a frame");
    if (!m_hot_csv.empty()) {
        LoadHotChannels(m_hot_csv);
    }
    if (m_finder_mode == "coincidence" && m_hot_channels.empty()) {
        LOG_WARN(GetLogger()) << "coincidence finder without a hot-channel list: noisy always-on channels will fake coincidences" << LOG_END;
    }
}

void SroFrameUnfolder::LoadHotChannels(const std::string& csv_path) {
    std::ifstream file(csv_path);
    if (!file) {
        throw JException("SroFrameUnfolder: cannot open hot-channel CSV %s", csv_path.c_str());
    }
    std::string line;
    std::getline(file, line); // header: rocid,slot,channel,group,hits_per_frame
    while (std::getline(file, line)) {
        std::istringstream fields(line);
        std::string rocid_text, slot_text, channel_text;
        if (std::getline(fields, rocid_text, ',') && std::getline(fields, slot_text, ',') && std::getline(fields, channel_text, ',')) {
            uint32_t rocid = std::stoul(rocid_text);
            uint32_t slot = std::stoul(slot_text);
            uint32_t channel = std::stoul(channel_text);
            m_hot_channels.insert((rocid << 16) | (slot << 8) | channel);
        }
    }
    LOG_INFO(GetLogger()) << "SroFrameUnfolder: loaded " << m_hot_channels.size() << " hot channels from " << m_hot_csv << LOG_END;
}

bool SroFrameUnfolder::FramePassesFinder(const sro::SroBlockData& block, uint32_t frame_index) const {
    const sro::FrameInfo& frame = block.frames[frame_index];

    if (m_finder_mode == "coincidence") {
        // Event = any time bin with >= m_min_hits_in_bin ECAL hits on non-hot
        // channels. 16384 FADC ticks per frame / bin width bins; the array is
        // small enough to live on the stack per call.
        uint32_t bin_count = 16384 / m_bin_ticks;
        std::vector<uint16_t> bins(bin_count, 0);
        for (uint32_t hit_i = frame.first_fadc_hit; hit_i < frame.first_fadc_hit + frame.fadc_hit_count; hit_i++) {
            const sro::FadcHit& hit = block.fadc_hits[hit_i];
            if (hit.detector != static_cast<int8_t>(sro::Detector::Ecal) || IsHot(hit.rocid, hit.slot, hit.channel)) {
                continue;
            }
            uint32_t bin = hit.time_ticks / m_bin_ticks;
            if (++bins[bin] >= m_min_hits_in_bin) {
                return true;
            }
        }
        return false;
    }

    if (m_finder_mode == "ecal") {
        uint64_t ecal_charge = 0;
        for (uint32_t hit_i = frame.first_fadc_hit; hit_i < frame.first_fadc_hit + frame.fadc_hit_count; hit_i++) {
            if (block.fadc_hits[hit_i].detector == static_cast<int8_t>(sro::Detector::Ecal)) {
                ecal_charge += block.fadc_hits[hit_i].charge;
            }
        }
        return ecal_charge >= m_min_ecal_charge;
    }

    return true; // bypass
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
