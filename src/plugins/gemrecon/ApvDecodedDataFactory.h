#pragma once

#include <JANA/JMultifactory.h>
#include <JANA/JFactoryGenerator.h>
#include <JANA/JFactoryT.h>
#include <JANA/JMultifactory.h>
#include <TH2F.h>
#include "rawdataparser/DGEMSRSWindowRawData.h"
#include "GemReconDqmProcessor.h"
#include "extensions/spdlog/SpdlogExtensions.h"
#include "SFclust.h"
#include "DecodedData.h"


namespace ml4fpga::gem {

    class ApvDecodedDataFactory: public JFactoryT<ApvDecodedData>, public spdlog::extensions::SpdlogMixin<ApvDecodedDataFactory>  {
        public:
            ApvDecodedDataFactory() = default;
            void Init() override;
            void Process(const std::shared_ptr<const JEvent>&) override;
            void Finish() override;
        private:

            AdcDecodedData DecodeApv(int apv_id, std::vector<std::vector<double>> raw_data,
                                     std::vector<double> offsets,
                                     std::vector<double> noises);

            unsigned int m_srs_ntsamples = 9;
            int m_min_adc = 50;         /// Min ADC value for histogram plotting
            int m_max_adc = 5000;       /// Max ADC value for histogram plotting
            std::string fIsHitPeakOrSumADCs = "peakADCs";
            std::string fIsCentralOrAllStripsADCs = "centralStripADCs";
        GemMapping *m_mapping;
        int m_bad_apv_warns = 0;    /// Limits malformed-APV warning printouts
    };
} // namespace ml4fpga::gem


