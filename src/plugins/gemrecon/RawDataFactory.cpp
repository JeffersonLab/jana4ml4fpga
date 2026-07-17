//
// Created by romanov on 5/8/2023.
// Subject to the terms in the LICENSE file found in the top-level directory.
//

#include "RawDataFactory.h"
#include "GemMapping.h"
#include "libraries/rawdataparser/DGEMSRSWindowRawData.h"
#include <JANA/JEvent.h>
#include <algorithm>

#include "GemMappingService.h"

namespace ml4fpga {
    namespace gem {
        class GemMappingService;
    }

    void gem::RawDataFactory::Init() {
        InitLogger(GetPluginName() + ":" + JTypeInfo::demangle<RawDataFactory>());
        auto mapping = GetApplication()->GetService<GemMappingService>()->GetMapping();
        m_apv_id_names_map = mapping->GetAPVFromIDMap();
    }

    void gem::RawDataFactory::Process(const std::shared_ptr<const JEvent> &event) {

        // TODO make it a service
        // TODO  It is purely coincidence that it is working and is properly initialized
        // (ok, not really a coincidence, but it is a bad code)
        auto fMapping = GemMapping::GetInstance();

        m_log->debug("new event");
        try {
            auto srs_data = event->Get<DGEMSRSWindowRawData>();
            m_log->trace("DGEMSRSWindowRawData data items: {} ", srs_data.size());

            //TraceDumpSrsData(srs_data, /*num rows*/ 256);
            //TraceDumpMapping(srs_data, /*num rows*/ 256);

            std::map<int, std::map<int, std::vector<int> > > apvid_chan_sampls;

            for (auto srs_item: srs_data) {

                // It might be EVIO contains APV that we don't need to process
                if(!m_apv_id_names_map.count((int) srs_item->apv_id)) continue;

                // Dumb copy of samples because one is int and the other is uint16_t
                std::vector<int> samples;
                for (unsigned short sample: srs_item->samples) {
                    samples.push_back(sample);
                }
                // m_log->info("ch {:<4} : mapped: {}", srs_item->channel_apv, Constants::ApvChannelCorrection(srs_item->channel_apv));


                apvid_chan_sampls[(int) srs_item->apv_id][Constants::ApvChannelCorrection(srs_item->channel_apv)] = samples;
                int bin_index = fMapping->APVchannelCorrection(srs_item->channel_apv);
                //;
            }

            // Now lets go over this
            // fec, apv, raw_data
            auto result = new RawData();

            for (auto apv_pair: apvid_chan_sampls) {
                auto apv_id = apv_pair.first;
                auto time_samples = apv_pair.second;

                // Number of time samples. Channels can be absent or have fewer samples in
                // malformed events, so take the maximum over channels instead of channel 0
                size_t samples_size = 0;
                for (auto& time_samples_pair: time_samples) {
                    samples_size = std::max(samples_size, time_samples_pair.second.size());
                }
                const size_t raw_data_len = samples_size * 128;
                std::vector<double> all_samples(raw_data_len, 0);

                RawSingleApvData apv_data;
                // go over time bins
                for (auto time_samples_pair: time_samples) {
                    auto timebin = time_samples_pair.first;
                    auto samples = time_samples_pair.second;
                    // go over samples
                    for (size_t sample_i = 0; sample_i < samples.size(); sample_i++) {
                        size_t data_index = sample_i * 128 + timebin;  // + sample_i to make gaps in between samples
                        if (data_index < all_samples.size()) {
                            all_samples[data_index] = samples[sample_i];
                        }
                    }
                }
                apv_data.all_samples = all_samples;
                apv_data.apv_id = apv_id;
                apv_data.det_name = fMapping->GetDetectorFromAPVIDMap()[apv_id];

                // Fec is always 0,
                result->data[apv_id] = apv_data;
            }

            Insert(result);
        }
        catch (std::exception& ex) {
            throw JException(ex.what());
        }
    }

    void gem::RawDataFactory::Finish() {
        JFactory::Finish();
    }

} // ml4fpga