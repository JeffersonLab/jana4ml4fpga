//
// Created by romanov on 3/7/2024.
// Subject to the terms in the LICENSE file found in the top-level directory.
//

#pragma once
#include <extensions/jana/CozyFactory.h>
#include <extensions/spdlog/SpdlogMixin.h>
#include <extmath/RollingStatistics.h>
#include <JANA/JFactoryT.h>
#include <rawdataparser/DGEMSRSWindowRawData.h>

#include "GemMappingService.h"
#include "SampleAnalysisData.h"
#include "SampleData.h"


namespace ml4fpga::gem {

    class SampleAnalysisFactory : public CozyFactory<EmptyConfig>{
    public:
        SampleAnalysisFactory()=default;

        void CozyInit() override;
        void CozyProcess(uint64_t run_number, uint64_t event_number) override;


    private:

        Input<DGEMSRSWindowRawData> m_srs_raw_data {this};
        Output<SampleData> m_output {this};
        Service<GemMappingService> m_mapping_service {this};

        /// User asserted number of timbe-bins
        int m_req_ntsamples;                        // Number of time samples set in flag

        /// Gem mapping (gem configuration)
        GemMapping* m_mapping;

        /// Map of {key:apv-id, value:apv-name}
        std::map<int, std::string> m_apv_names_by_id;

        /// True = filtration flag is set by channel n*sigma, fixed threshold otherwise
        bool m_filter_by_sigma = true;

        Parameter<std::string> m_cfg_host {this, "host", "localhost", "Host address to connect to"};
        Parameter<size_t> m_rolling_len {this, "stat_len", 500, "Rolling average and sigma length for each sample"};
        Parameter<std::string> m_filter_algo_name {this, "filter_algo", "sigma", "'sigma' - filter by x*sigma, 'trheshold' filter by fixed threshold"};
        Parameter<double> m_filter_sigmas {this, "filter_sigmas", 3, "Sets X*sigmas for sample filtration. Works only if filter_algo=='sigma'"};
        Parameter<double> m_filter_threshold {this, "filter_threshold", 3, "Sets X*sigmas for sample filtration. Works only if filter_algo=='threshold'"};


        std::map<uint64_t, extmath::RollingStatistics<double>> m_sample_stats;


};

} // gem
// ml4fpga
