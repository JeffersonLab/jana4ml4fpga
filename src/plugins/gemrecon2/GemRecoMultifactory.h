// gemrecon2: the ONE thin adapter factory - marshals EVIO data into the gemreco
// library and republishes RecoResult as the standard JANA data objects.
// Replaces the seven legacy gemrecon factories (MT plan W2-F).
#pragma once

#include <JANA/JMultifactory.h>

#include <gemreco/workspace.hpp>
#include <plugins/gemrecon/DecodedData.h>
#include <plugins/gemrecon/PlanePeak.h>
#include <plugins/gemrecon/SFclust.h>
#include <plugins/gemrecon/SampleData.h>

#include "GemRecoService.h"

class GemRecoMultifactory : public JMultifactory {
public:
    GemRecoMultifactory() {
        DeclareOutput<ml4fpga::gem::PlanePeak>("");
        DeclareOutput<ml4fpga::gem::PlanePeakFindingResult>("");
        DeclareOutput<ml4fpga::gem::SFclust>("");
        DeclareOutput<ml4fpga::gem::PlaneDecodedData>("");
        DeclareOutput<ml4fpga::gem::SampleData>("");
    }

    void Init() override;
    void Process(const std::shared_ptr<const JEvent>& event) override;

private:
    std::shared_ptr<GemRecoService> m_service;
    std::shared_ptr<spdlog::logger> m_log;
};
