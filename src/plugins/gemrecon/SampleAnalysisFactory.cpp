//
// Created by romanov on 3/7/2024.
// Subject to the terms in the LICENSE file found in the top-level directory.
//

#include "SampleAnalysisFactory.h"

#include "Constants.h"

namespace ml4fpga::gem {
    void SampleAnalysisFactory::CozyInit() {
        // In general CozyFactories should not use direct App and GetParameter
        // But we do it to check the consistency and we are sure this parameter is set in gemrecon
        GetApplication()->GetParameter("daq:srs_window_raw:ntsamples", m_req_ntsamples);

        m_mapping = m_mapping_service().GetMapping();
        m_apv_names_by_id = m_mapping->GetAPVFromIDMap();
        m_mapping->GetAPVIndexOnPlaneFromIDMap();
        m_filter_by_sigma = m_filter_algo_name() == "sigma";


    }

    void SampleAnalysisFactory::CozyProcess(uint64_t run_number, uint64_t event_number) {

        if(m_srs_raw_data().empty()) return;    // Nothing to do here

        // Check time bins size consistency
        auto evio_samples_size = m_srs_raw_data()[0]->samples.size();
        if (evio_samples_size != m_req_ntsamples) {
            logger()->warn("Parameter daq:srs_window_raw:ntsamples = {} != DGEMSRSWindowRawData.samples.size() from EVIO. Is parameter value correct? At event# = {}",
                m_req_ntsamples,  evio_samples_size, event_number);
        }

        // Results sorted by ID (as std::maps guarantees key sorting)
        // Using ID (defined below) for sorting gives us samples order:
        // detector -> plane -> time bin -> apv on plane -> channel num
        std::map<uint64_t, SampleData*> m_sorted_results;

        for (auto srs_item: m_srs_raw_data()) {

            // It might be EVIO contains APV that we don't need to process
            if(!m_apv_names_by_id.count((int) srs_item->apv_id)) continue;

            // Dumb copy of samples because one is int and the other is uint16_t
            std::vector<int> samples;
            for (int sample_index=0; sample_index < srs_item->samples.size(); sample_index++) {
                auto sample = srs_item->samples[sample_index];
                auto result = new SampleData();

                // Claculate channel address inside APV
                result->raw_channel = srs_item->channel_apv;
                result->channel = Constants::ApvChannelCorrection(srs_item->channel_apv);
                if(m_mapping->GetAPVOrientation(result->apv)) {
                    result->channel = Constants::ChannelsCount - 1 - result->channel;
                }

                // Set IDs: APV, plane id, detector id, time_bin, composit id
                result->apv = srs_item->apv_id;
                result->time_bin = sample_index;
                auto plane_name = m_mapping->GetPlaneFromAPVID(result->apv);
                auto plane_id = m_mapping->GetPlaneID(plane_name);
                result->plane = plane_id;
                auto det_name = m_mapping->GetDetectorFromAPVIDMap()[result->apv];
                result->detector = m_mapping->GetDetectorID(det_name);
                uint64_t apv_index_on_plane = m_mapping->GetAPVIndexOnPlane(result->apv);

                // Make sample ID
                result->id = result->channel +
                             result->apv*1000 +
                             apv_index_on_plane*1000000 +
                             result->time_bin*1000000000 +
                             result->plane*1000000000000 +
                             result->detector*1000000000000000+
                             1000000000000000000;   // Why there is no 1e18i in c++? Sigh...

                // Set sample value
                result->value = sample;
                result->raw_value = sample;

                // Add value to statistics calcualtion
                if(!m_sample_stats.count(result->id)) {
                    m_sample_stats[result->id] = extmath::RollingStatistics<double>(m_rolling_len());
                }
                auto &stat = m_sample_stats[result->id];
                stat.add(result->value);
                result->rolling_average = stat.getAverage();
                result->rolling_std = stat.getStandardDeviation();

                // Substract background
                result->value = -(result->raw_value - result->rolling_average);

                // Set filtration flag
                if(m_filter_by_sigma) {
                    // N*Sigmas method
                    result->is_noise = std::abs(result->raw_value - result->rolling_average) < m_filter_sigmas() * result->rolling_std;
                }
                else {
                    // Fixed threshold method
                    result->is_noise = std::abs(result->raw_value - result->rolling_average) < m_filter_threshold();
                }

                m_sorted_results[result->id] = result;
            }
        }

        // We want to guarantee that the output results are in the order:
        // detector -> plane -> time bin -> apv on plane -> channel num
        for(auto pair: m_sorted_results) {
            m_output().push_back(pair.second);
        }
    }
} // ml4fpga