
#pragma once

#include <cstdint>
#include <vector>
#include <TTree.h>
#include "AlignedArraysIO.h"

namespace flatio {
    struct GemSampleData
    {

        uint64_t id;
        uint32_t channel;
        uint32_t raw_channel;
        uint32_t time_bin;
        uint32_t apv;
        uint32_t plane;
        uint32_t detector;
        bool is_noise;
        double value;
        double raw_value;
        double rolling_average;
        double rolling_std;
    };
    
    class GemSampleDataIO: public AlignedArraysIO
    {
    public:
        void bindToTree(TTree *tree) override {
            m_is_bound = true;
            tree->Branch("gem_sample_data_count", &m_count, "gem_sample_data_count/l");

            tree->Branch("gem_sample_data_id", &m_vect_id);
            tree->Branch("gem_sample_data_channel", &m_vect_channel);
            tree->Branch("gem_sample_data_raw_channel", &m_vect_raw_channel);
            tree->Branch("gem_sample_data_time_bin", &m_vect_time_bin);
            tree->Branch("gem_sample_data_apv", &m_vect_apv);
            tree->Branch("gem_sample_data_plane", &m_vect_plane);
            tree->Branch("gem_sample_data_detector", &m_vect_detector);
            tree->Branch("gem_sample_data_is_noise", &m_vect_is_noise);
            tree->Branch("gem_sample_data_value", &m_vect_value);
            tree->Branch("gem_sample_data_raw_value", &m_vect_raw_value);
            tree->Branch("gem_sample_data_rolling_average", &m_vect_rolling_average);
            tree->Branch("gem_sample_data_rolling_std", &m_vect_rolling_std);
        }

        void clear() override {
            m_count = 0;

            m_vect_id.clear();
            m_vect_channel.clear();
            m_vect_raw_channel.clear();
            m_vect_time_bin.clear();
            m_vect_apv.clear();
            m_vect_plane.clear();
            m_vect_detector.clear();
            m_vect_is_noise.clear();
            m_vect_value.clear();
            m_vect_raw_value.clear();
            m_vect_rolling_average.clear();
            m_vect_rolling_std.clear();
        }
        
        void add(const GemSampleData& data) {
            if(!m_is_bound) {
                throw std::logic_error("Can't add GemSampleData data because GemSampleDataIO is not bound to tree");
            }
            m_count++;

            m_vect_id.push_back(data.id);
            m_vect_channel.push_back(data.channel);
            m_vect_raw_channel.push_back(data.raw_channel);
            m_vect_time_bin.push_back(data.time_bin);
            m_vect_apv.push_back(data.apv);
            m_vect_plane.push_back(data.plane);
            m_vect_detector.push_back(data.detector);
            m_vect_is_noise.push_back(data.is_noise);
            m_vect_value.push_back(data.value);
            m_vect_raw_value.push_back(data.raw_value);
            m_vect_rolling_average.push_back(data.rolling_average);
            m_vect_rolling_std.push_back(data.rolling_std);
        }
        
        bool isBoundToTree() const { return m_is_bound; }
        
    private:
        bool m_is_bound = false;
        uint64_t m_count;

        std::vector<uint64_t> m_vect_id;
        std::vector<uint32_t> m_vect_channel;
        std::vector<uint32_t> m_vect_raw_channel;
        std::vector<uint32_t> m_vect_time_bin;
        std::vector<uint32_t> m_vect_apv;
        std::vector<uint32_t> m_vect_plane;
        std::vector<uint32_t> m_vect_detector;
        std::vector<bool> m_vect_is_noise;
        std::vector<double> m_vect_value;
        std::vector<double> m_vect_raw_value;
        std::vector<double> m_vect_rolling_average;
        std::vector<double> m_vect_rolling_std;
    };
}
