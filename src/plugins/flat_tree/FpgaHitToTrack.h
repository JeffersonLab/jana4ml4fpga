
#pragma once

#include <cstdint>
#include <vector>
#include <TTree.h>
#include "AlignedArraysIO.h"

namespace flatio {
    struct FpgaHitToTrack
    {

        uint32_t hit_index;
        uint32_t track_index;
    };
    
    class FpgaHitToTrackIO: public AlignedArraysIO
    {
    public:
        void bindToTree(TTree *tree) override {
            m_is_bound = true;
            tree->Branch("fpga_hit_track_count", &m_count, "fpga_hit_track_count/l");

            tree->Branch("fpga_hit_track_hit_index", &m_vect_hit_index);
            tree->Branch("fpga_hit_track_track_index", &m_vect_track_index);
        }

        void clear() override {
            m_count = 0;

            m_vect_hit_index.clear();
            m_vect_track_index.clear();
        }
        
        void add(const FpgaHitToTrack& data) {
            if(!m_is_bound) {
                throw std::logic_error("Can't add FpgaHitToTrack data because FpgaHitToTrackIO is not bound to tree");
            }
            m_count++;

            m_vect_hit_index.push_back(data.hit_index);
            m_vect_track_index.push_back(data.track_index);
        }
        
        bool isBoundToTree() const { return m_is_bound; }
        
    private:
        bool m_is_bound = false;
        uint64_t m_count;

        std::vector<uint32_t> m_vect_hit_index;
        std::vector<uint32_t> m_vect_track_index;
    };
}
