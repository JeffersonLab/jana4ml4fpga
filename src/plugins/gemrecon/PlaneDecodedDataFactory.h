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
#include "GemMapping.h"


namespace ml4fpga::gem {

    class PlaneDecodedDataFactory:
        public JFactoryT<PlaneDecodedData>,
        public spdlog::extensions::SpdlogMixin<PlaneDecodedDataFactory>  {
    public:
        PlaneDecodedDataFactory() = default;
        void Init() override;
        void Process(const std::shared_ptr<const JEvent>&) override;
        void Finish() override;
    private:
        AdcDecodedData GetApvDataOrZeros(const ApvDecodedData &decoded, int apv_id,
                                         size_t n_timebins, const std::string &plane_name);

        unsigned int m_srs_ntsamples = 9;
        int m_min_adc = 50;         /// Min ADC value for histogram plotting
        int m_max_adc = 5000;       /// Max ADC value for histogram plotting
        std::string fIsHitPeakOrSumADCs = "peakADCs";
        std::string fIsCentralOrAllStripsADCs = "centralStripADCs";
        GemMapping *m_mapping;
        int m_missing_apv_warns = 0;    /// Limits missing-APV warning printouts
    };
} // namespace ml4fpga::gem


