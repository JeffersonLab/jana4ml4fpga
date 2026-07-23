
#pragma once

#include <cstdint>
#include <vector>
#include <TTree.h>
#include "AlignedArraysIO.h"

namespace flatio {
    struct FpgaTrackFit
    {
        uint32_t id;
        float slope;
        float intersect;
    };
    
    class FpgaTrackFitIO: public AlignedArraysIO
    {
    public:
        void bindToTree(TTree *tree) override {
            m_is_bound = true;
            tree->Branch("fpga_track_fit_count", &m_count, "fpga_track_fit_count/l");

            tree->Branch("fpga_track_fit_id", &m_vect_id);
            tree->Branch("fpga_track_fit_slope", &m_vect_slope);
            tree->Branch("fpga_track_fit_intersect", &m_vect_intersect);
        }

        void clear() override {
            m_count = 0;

            m_vect_id.clear();
            m_vect_slope.clear();
            m_vect_intersect.clear();
        }
        
        void add(const FpgaTrackFit& data) {
            if(!m_is_bound) {
                throw std::logic_error("Can't add FpgaTrackFit data because FpgaTrackFitIO is not bound to tree");
            }
            m_count++;

            m_vect_id.push_back(data.id);
            m_vect_slope.push_back(data.slope);
            m_vect_intersect.push_back(data.intersect);
        }
        
        bool isBoundToTree() const { return m_is_bound; }
        
    private:
        bool m_is_bound = false;
        uint64_t m_count;

        std::vector<uint32_t> m_vect_id;
        std::vector<float> m_vect_slope;
        std::vector<float> m_vect_intersect;
    };
}
