//
// Created by romanov on 2/27/2024.
// Subject to the terms in the LICENSE file found in the top-level directory.
//

#pragma once


namespace ml4fpga::gem {
    class SampleData {
    public:
        uint64_t id;        /// ID, made of  channel, time_bin, apv, apv_index_on_plane, plane_id, detector_id
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
}



