// gemrecon2: the ONE thin adapter factory (JANA2 2026.x JOmniFactory version) -
// marshals EVIO data into the gemreco library and republishes RecoResult as the
// standard JANA data objects. Replaces both the seven legacy gemrecon factories
// and the earlier JMultifactory-based adapter.
#pragma once

#include <JANA/Components/JOmniFactory.h>

#include <rawdataparser/DGEMSRSWindowRawData.h>
#include <plugins/gemrecon/DecodedData.h>
#include <plugins/gemrecon/PlanePeak.h>
#include <plugins/gemrecon/SFclust.h>
#include <plugins/gemrecon/SampleData.h>

#include "GemRecoService.h"

class GemRecoOmniFactory : public JOmniFactory<GemRecoOmniFactory> {
private:
    Input<DGEMSRSWindowRawData> m_srs_in {this};

    Output<ml4fpga::gem::PlanePeak> m_peaks_out {this};
    Output<ml4fpga::gem::PlanePeakFindingResult> m_peak_result_out {this};
    Output<ml4fpga::gem::SFclust> m_clusters_out {this};
    Output<ml4fpga::gem::PlaneDecodedData> m_plane_data_out {this};
    Output<ml4fpga::gem::SampleData> m_samples_out {this};

    Service<GemRecoService> m_service {this};

public:
    GemRecoOmniFactory() = default;

    void Configure() {}
    void ChangeRun(int32_t /*run_nr*/) {}
    void Execute(int32_t run_nr, uint64_t event_nr);
};
