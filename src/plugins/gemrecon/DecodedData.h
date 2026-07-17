//
// Created by romanov on 5/9/2023.
// Subject to the terms in the LICENSE file found in the top-level directory.
//

#pragma once

#include <map>
#include <vector>

namespace ml4fpga::gem {
    class AdcDecodedData {
    public:
        std::vector<double> PedestalOffsets;
        std::vector<double> PedestalNoises;
        std::vector<double> CommonModeOffsets;
        std::vector<double> RawDataAverage;
        std::vector<std::vector<double>> raw_data;
        std::vector<std::vector<double>> data;
        std::string plane_name;
        std::string detector_name;
        std::vector<int> apv_ids;
    };


    struct ApvDecodedData {
        std::map<int, AdcDecodedData> apv_data;
    };

    struct PlaneDecodedData {
        std::map<std::string, AdcDecodedData> plane_data;
    };

    inline AdcDecodedData mergeAdcDecodedData(const AdcDecodedData& first, const AdcDecodedData& second) {
        AdcDecodedData merged;

        merged.PedestalOffsets = first.PedestalOffsets;
        merged.PedestalOffsets.insert(merged.PedestalOffsets.end(), second.PedestalOffsets.begin(), second.PedestalOffsets.end());

        merged.PedestalNoises = first.PedestalNoises;
        merged.PedestalNoises.insert(merged.PedestalNoises.end(), second.PedestalNoises.begin(), second.PedestalNoises.end());

        merged.CommonModeOffsets = first.CommonModeOffsets;
        merged.CommonModeOffsets.insert(merged.CommonModeOffsets.end(), second.CommonModeOffsets.begin(), second.CommonModeOffsets.end());

        merged.RawDataAverage = first.RawDataAverage;
        merged.RawDataAverage.insert(merged.RawDataAverage.end(), second.RawDataAverage.begin(), second.RawDataAverage.end());

        merged.apv_ids = first.apv_ids;
        merged.apv_ids.insert(merged.apv_ids.end(), second.apv_ids.begin(), second.apv_ids.end());

        // `second` can have more time-bin rows than `first` (missing APV in the event,
        // truncated frame, etc.). Grow the destination instead of writing past its end.
        merged.raw_data = first.raw_data;
        if (second.raw_data.size() > merged.raw_data.size()) {
            merged.raw_data.resize(second.raw_data.size());
        }
        for(size_t i = 0; i < second.raw_data.size(); ++i) {
            merged.raw_data[i].insert(merged.raw_data[i].end(), second.raw_data[i].begin(), second.raw_data[i].end());
        }

        merged.data = first.data;
        if (second.data.size() > merged.data.size()) {
            merged.data.resize(second.data.size());
        }
        for(size_t i = 0; i < second.data.size(); ++i) {
            merged.data[i].insert(merged.data[i].end(), second.data[i].begin(), second.data[i].end());
        }

        // Please handle merging of plane_name and detector_name according to your requirements
        return merged;
    }
}
