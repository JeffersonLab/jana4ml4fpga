#include "ApvDecodedDataFactory.h"

#include "rawdataparser/DGEMSRSWindowRawData.h"
#include "rawdataparser/Df125WindowRawData.h"
#include "rawdataparser/Df125FDCPulse.h"

#include <JANA/JApplication.h>
#include <JANA/JEvent.h>

#include <Math/GenVector/PxPyPzM4D.h>

#include <spdlog/spdlog.h>
#include "services/root_output/RootFile_service.h"
#include <filesystem>
#include <numeric>

#include "GemMapping.h"
#include "Pedestal.h"
#include "RawData.h"

namespace ml4fpga::gem {



//-------------------------------------
// Init
//-------------------------------------
void ApvDecodedDataFactory::Init() {

    std::string plugin_name = GetPluginName();

    // Get JANA application
    auto app = GetApplication();

    // Get Log level from user parameter or default
    InitLogger(plugin_name + ":ApvDecode");

    // P A R A M E T E R S
    // Number of SRS time samples:
    app->SetDefaultParameter("daq:srs_window_raw:ntsamples", m_srs_ntsamples, "Number of SRS time samples");
    app->SetDefaultParameter( plugin_name + ":min_adc", m_min_adc, "Min ADC value (For histos?)");
    app->SetDefaultParameter( plugin_name + ":max_adc", m_max_adc, "Max ADC value (For histos?)");

    //  D O N E
    logger()->info("This plugin name is: " + GetPluginName());
    logger()->info("DecodedDataFactory initialization is done");

    m_mapping = GemMapping::GetInstance();
}



//------------------
// Process
//------------------
// This function is called every event
void ApvDecodedDataFactory::Process(const std::shared_ptr<const JEvent> &event) {
    m_log->debug("new event");
    try {
        auto raw_data = event->GetSingle<RawData>();
        if (raw_data == nullptr) {
            m_log->debug("RawData is null. No SRS data in this event? Skipping");
            return;
        }
        auto pedestal = event->GetSingle<ml4fpga::gem::Pedestal>();
        if (pedestal == nullptr) {
            m_log->debug("Pedestal is null. Skipping event");
            return;
        }

        auto apv_decoded_data = new ApvDecodedData();

        for(auto& apv_pair: raw_data->data) {
            auto apv_id = apv_pair.first;
            auto apv_samples = apv_pair.second;

            auto timebins = apv_samples.AsTimebins();
            auto offsets_it = pedestal->offsets.find(apv_id);
            auto noises_it = pedestal->noises.find(apv_id);

            // Skip APVs with truncated/malformed data or missing pedestals instead of
            // reading out of bounds in DecodeApv. Skipped APVs are zero-substituted
            // later in PlaneDecodedDataFactory
            bool pedestals_ok = offsets_it != pedestal->offsets.end() && noises_it != pedestal->noises.end()
                                && offsets_it->second.size() == Constants::ChannelsCount
                                && noises_it->second.size() == Constants::ChannelsCount;
            if (timebins.empty() || !pedestals_ok) {
                if (m_bad_apv_warns < 20) {
                    m_bad_apv_warns++;
                    m_log->warn("APV {}: malformed data or pedestals (timebins={}, pedestals_ok={}). Skipping APV.{}",
                                apv_id, timebins.size(), pedestals_ok,
                                m_bad_apv_warns == 20 ? " (Further warnings suppressed)" : "");
                }
                continue;
            }

            auto data = DecodeApv(apv_id, timebins, offsets_it->second, noises_it->second);
            data.plane_name = m_mapping->GetPlaneFromAPVID(apv_id);
            data.detector_name = m_mapping->GetDetectorFromPlane(data.plane_name);
            data.apv_ids = {apv_id};
            apv_decoded_data->apv_data[apv_id] = data;
        }

        Insert(apv_decoded_data);
    }
    catch (std::exception &exp) {
        m_log->error("Error during process");
        m_log->error("Exception what()='{}', type='{}'", exp.what(), typeid(exp).name());
    }
}


//------------------
// Finish
//------------------
void ApvDecodedDataFactory::Finish() {
//    m_log->trace("DecodedDataFactory finished\n");

}


    AdcDecodedData ApvDecodedDataFactory::DecodeApv(int apv_id, std::vector<std::vector<double>> raw_data,
                                                 std::vector<double> offsets,
                                                 std::vector<double> noises) {
    //===========================================================================================================
    // BLOCK WHERE COMMON MODE CORRECTION FOR ALL APV25 TIME BINS IS COMPUTED
    //===========================================================================================================

    int fComModeCut = 20;       // TODO parameter
    double fZeroSupCut = 10;    // TODO parameter
    int fAPVBaseline = 2500;
    int time_bins_size = raw_data.size();

    // Asserts compile out with NDEBUG, so check malformed inputs at runtime too.
    // Empty result is treated as a missing APV downstream.
    if (time_bins_size <= 0 || raw_data[0].size() != Constants::ChannelsCount
        || offsets.size() != Constants::ChannelsCount) {
        return AdcDecodedData();
    }

    std::vector<double> commonModeOffsets(time_bins_size, 0);
    std::vector<double> rawDataZS(time_bins_size, 0);

    for (Int_t timebin = 0; timebin < time_bins_size; timebin++) {
        std::vector<double> raw_channel_values = raw_data[timebin];

        // PERFORM APV25 PEDESTAL OFFSET CORRECTION FOR A GIVEN TIME BIN
        std::vector<double> channel_values;
        for(size_t i=0; i < raw_channel_values.size(); i++) {
            channel_values.push_back(raw_channel_values[i] - offsets[i]);
        }

        std::map<double, int> values_index_map;
        for (int i = 0; i < Constants::ChannelsCount; i++) {
            double val = channel_values[i];
            values_index_map[val] = i;
        }

        // Select only N channels with lowest adc
        std::vector<double> dataTest = channel_values;
        std::sort(dataTest.begin(), dataTest.end());
        assert(fComModeCut < 28);
        for (int i = 0; i < fComModeCut; i++) {
            //     if(fAPVID == 0) printf("\n Enter  GEMHitDecoder::APVEventDecoder()=>BF: data[%d]=%f \n",timebin,dataTest[i]) ;
            dataTest[i] = -offsets[values_index_map[dataTest[i]]] + fAPVBaseline;
            //     if(fAPVID == 0) printf(" Enter  GEMHitDecoder::APVEventDecoder()=>AF: data[%d]=%f \n",  timebin,dataTest[i]) ;
        }

        // COMPUTE COMMON MODE FOR A GIVEN APV AND TIME BIN
        double commonMode = std::accumulate(dataTest.begin(), dataTest.end(), 0.0) / Constants::ChannelsCount;
        commonModeOffsets[timebin] = commonMode;
        //    if(fAPVID == 0) printf(" Enter  GEMHitDecoder::APVEventDecoder(), timebin = %d, commonMode = %d \n",  timebin, commonMode) ;

        // PERFORM COMMON MODE CORRECTION FOR A GIVEN TIME BIN
        // (std::bind2nd was removed in C++17)
        std::transform(channel_values.begin(), channel_values.end(), channel_values.begin(),
                       [commonMode](double value) { return value - commonMode; });

        //  ADC SUM OVER ALL TIME BINS USE AS THE TEST CRITERIA FOR ZERO SUPPRESSION
        for(size_t i=0; i < rawDataZS.size() && i < channel_values.size(); i++) {
            rawDataZS[i] += channel_values[i];
        }
    }

    // ADC AVERAGE OVER ALL TIME BINS USE AS THE TEST CRITERIA FOR ZERO SUPPRESSION
    for(int i=0; i < rawDataZS.size(); i++) {
        rawDataZS[i] = rawDataZS[i]/time_bins_size;
    }

    //if (isCommonModeTooLarge) return;

    std::vector<std::vector<double>> decoded_data;

    for (size_t timebin = 0; timebin < raw_data.size(); timebin++) {
        // EXTRACT APV25 DATA FOR A GIVEN TIME BIN
        std::vector<double> timebin_data(Constants::ChannelsCount);
        std::vector<double> rawDataTS = raw_data[timebin];
        for (int ch_i = 0; ch_i < Constants::ChannelsCount; ch_i++) {

            double data = -(rawDataTS[ch_i] - offsets[ch_i] - commonModeOffsets[timebin]);
            //int decoded_index = Constants::ApvChannelCorrection(ch_i);
            timebin_data[ch_i] = data;
        }
        decoded_data.push_back(timebin_data);
    }
    AdcDecodedData result;
    result.data = decoded_data;
    result.raw_data = raw_data;
    result.PedestalOffsets = offsets;
    result.PedestalNoises = noises;
    result.CommonModeOffsets = commonModeOffsets;
    result.RawDataAverage = rawDataZS;
    return result;

}
}
