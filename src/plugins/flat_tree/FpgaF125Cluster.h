
#pragma once

#include <cstdint>
#include <vector>
#include <TTree.h>
#include "AlignedArraysIO.h"

namespace flatio {
    struct FpgaF125Cluster
    {

        uint32_t id;
        float pos_x;
        float pos_y;
        float pos_z;
        float dedx;
        float size;
        float width_y1;
        float width_y2;
        float width_dy;
        float length_x1;
        float length_x2;
        float length_dx;
    };
    
    class FpgaF125ClusterIO: public AlignedArraysIO
    {
    public:
        void bindToTree(TTree *tree) override {
            m_is_bound = true;
            tree->Branch("fpga_f125_cluster_count", &m_count, "fpga_f125_cluster_count/l");

            tree->Branch("fpga_f125_cluster_id", &m_vect_id);
            tree->Branch("fpga_f125_cluster_pos_x", &m_vect_pos_x);
            tree->Branch("fpga_f125_cluster_pos_y", &m_vect_pos_y);
            tree->Branch("fpga_f125_cluster_pos_z", &m_vect_pos_z);
            tree->Branch("fpga_f125_cluster_dedx", &m_vect_dedx);
            tree->Branch("fpga_f125_cluster_size", &m_vect_size);
            tree->Branch("fpga_f125_cluster_width_y1", &m_vect_width_y1);
            tree->Branch("fpga_f125_cluster_width_y2", &m_vect_width_y2);
            tree->Branch("fpga_f125_cluster_width_dy", &m_vect_width_dy);
            tree->Branch("fpga_f125_cluster_length_x1", &m_vect_length_x1);
            tree->Branch("fpga_f125_cluster_length_x2", &m_vect_length_x2);
            tree->Branch("fpga_f125_cluster_length_dx", &m_vect_length_dx);
        }

        void clear() override {
            m_count = 0;

            m_vect_id.clear();
            m_vect_pos_x.clear();
            m_vect_pos_y.clear();
            m_vect_pos_z.clear();
            m_vect_dedx.clear();
            m_vect_size.clear();
            m_vect_width_y1.clear();
            m_vect_width_y2.clear();
            m_vect_width_dy.clear();
            m_vect_length_x1.clear();
            m_vect_length_x2.clear();
            m_vect_length_dx.clear();
        }
        
        void add(const FpgaF125Cluster& data) {
            if(!m_is_bound) {
                throw std::logic_error("Can't add FpgaF125Cluster data because FpgaF125ClusterIO is not bound to tree");
            }
            m_count++;

            m_vect_id.push_back(data.id);
            m_vect_pos_x.push_back(data.pos_x);
            m_vect_pos_y.push_back(data.pos_y);
            m_vect_pos_z.push_back(data.pos_z);
            m_vect_dedx.push_back(data.dedx);
            m_vect_size.push_back(data.size);
            m_vect_width_y1.push_back(data.width_y1);
            m_vect_width_y2.push_back(data.width_y2);
            m_vect_width_dy.push_back(data.width_dy);
            m_vect_length_x1.push_back(data.length_x1);
            m_vect_length_x2.push_back(data.length_x2);
            m_vect_length_dx.push_back(data.length_dx);
        }
        
        bool isBoundToTree() const { return m_is_bound; }
        
    private:
        bool m_is_bound = false;
        uint64_t m_count;

        std::vector<uint32_t> m_vect_id;
        std::vector<float> m_vect_pos_x;
        std::vector<float> m_vect_pos_y;
        std::vector<float> m_vect_pos_z;
        std::vector<float> m_vect_dedx;
        std::vector<float> m_vect_size;
        std::vector<float> m_vect_width_y1;
        std::vector<float> m_vect_width_y2;
        std::vector<float> m_vect_width_dy;
        std::vector<float> m_vect_length_x1;
        std::vector<float> m_vect_length_x2;
        std::vector<float> m_vect_length_dx;
    };
}
