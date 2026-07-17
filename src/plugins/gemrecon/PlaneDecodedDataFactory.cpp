#include "PlaneDecodedDataFactory.h"

#include "rawdataparser/Df125WindowRawData.h"

#include <JANA/JEvent.h>

#include <filesystem>
#include "Pedestal.h"
#include "RawData.h"

namespace ml4fpga::gem {


//-------------------------------------
// Init
//-------------------------------------
void PlaneDecodedDataFactory::Init() {

    std::string plugin_name = GetPluginName();

    // Get JANA application
    auto app = GetApplication();

    // Get Log level from user parameter or default
    InitLogger(plugin_name + ":PlaneDecode");

    // P A R A M E T E R S
    // Number of SRS time samples:
    app->SetDefaultParameter("daq:srs_window_raw:ntsamples", m_srs_ntsamples, "Number of SRS time samples");
    app->SetDefaultParameter(plugin_name + ":min_adc", m_min_adc, "Min ADC value (For hists?)");
    app->SetDefaultParameter(plugin_name + ":max_adc", m_max_adc, "Max ADC value (For hists?)");

    //  D O N E
    logger()->info("This plugin name is: " + GetPluginName());
    logger()->info("DecodedDataFactory initialization is done");

    m_mapping = GemMapping::GetInstance();
}


//------------------
// Process
//------------------
// This function is called every event
void PlaneDecodedDataFactory::Process(const std::shared_ptr<const JEvent> &event) {
    m_log->debug("new event");
    try {
        auto apv_decoded_data = event->GetSingle<ApvDecodedData>();
        if (apv_decoded_data == nullptr) {
            m_log->debug("ApvDecodedData is null. No decoded data? Skipping event");
            return;
        }

        auto result = new PlaneDecodedData();

        // Now go per plane
        auto plane_map = m_mapping->GetAPVIDListFromPlaneMap();
        for (auto pair: plane_map) {
            auto name = pair.first;
            auto det_name = m_mapping->GetDetectorFromPlane(name);
            auto apv_list = pair.second;

            // Find the data shape (number of time bins) from any APV of this plane
            // that has data in this event
            size_t n_timebins = 0;
            for (auto apv_id: apv_list) {
                auto it = apv_decoded_data->apv_data.find(apv_id);
                if (it != apv_decoded_data->apv_data.end() && !it->second.raw_data.empty()) {
                    n_timebins = it->second.raw_data.size();
                    break;
                }
            }

            if (n_timebins == 0) {
                // No data for this plane in this event. Keep the (empty) plane record
                // so downstream plane_data.at(name) doesn't throw
                m_log->debug("No APV data for plane {} in this event", name);
                result->plane_data[name] = AdcDecodedData();
                continue;
            }

            // Make a merge for all APVs
            AdcDecodedData merged_data;
            size_t i=0;
            for (auto apv_id: apv_list) {
                auto apv_data = GetApvDataOrZeros(*apv_decoded_data, apv_id, n_timebins, name);
                if(i==0) {
                    merged_data = apv_data;
                } else {
                    merged_data = mergeAdcDecodedData(merged_data, apv_data);
                }
                i++;
            }
            result->plane_data[name] = merged_data;
        }

        Insert(result);
    }
    catch (std::exception &exp) {
        m_log->error("Error during process");
        m_log->error("Exception what()='{}', type='{}'", exp.what(), typeid(exp).name());
    }
}


//------------------
// GetApvDataOrZeros
//------------------
// Returns decoded data of the given APV. If the APV is absent in this event or its
// data has an unexpected number of time bins (e.g. truncated frame), returns
// zero-filled data of the right shape so strip numbering inside the plane stays
// aligned and merging never reads/writes out of bounds.
AdcDecodedData PlaneDecodedDataFactory::GetApvDataOrZeros(const ApvDecodedData &decoded, int apv_id,
                                                          size_t n_timebins, const std::string &plane_name) {
    auto it = decoded.apv_data.find(apv_id);
    if (it != decoded.apv_data.end() && it->second.raw_data.size() == n_timebins) {
        return it->second;
    }

    if (m_missing_apv_warns < 20) {
        m_missing_apv_warns++;
        m_log->warn("APV {} of plane {} is missing or malformed in this event. Substituting zeros.{}",
                    apv_id, plane_name, m_missing_apv_warns == 20 ? " (Further warnings suppressed)" : "");
    }

    AdcDecodedData zeros;
    zeros.PedestalOffsets.assign(Constants::ChannelsCount, 0);
    zeros.PedestalNoises.assign(Constants::ChannelsCount, 0);
    zeros.CommonModeOffsets.assign(n_timebins, 0);
    zeros.RawDataAverage.assign(n_timebins, 0);
    zeros.raw_data.assign(n_timebins, std::vector<double>(Constants::ChannelsCount, 0));
    zeros.data.assign(n_timebins, std::vector<double>(Constants::ChannelsCount, 0));
    zeros.plane_name = plane_name;
    zeros.detector_name = m_mapping->GetDetectorFromPlane(plane_name);
    zeros.apv_ids = {apv_id};
    return zeros;
}


//------------------
// Finish
//------------------
void PlaneDecodedDataFactory::Finish() {
//    m_log->trace("DecodedDataFactory finished\n");

}

}
