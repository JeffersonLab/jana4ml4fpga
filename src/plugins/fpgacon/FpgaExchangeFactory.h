//
// Created by Dmitry Romanov on 3/8/2024.
// Subject to the terms in the LICENSE file found in the top-level directory.
//

#pragma once

#include <extensions/jana/CozyFactory.h>
#include <extensions/spdlog/SpdlogMixin.h>

#include <JANA/JFactoryT.h>
#include <TSocket.h>

#include "F125Cluster.h"
#include "FpgaTrackFit.h"
#include "FpgaHitsToTrack.h"
#include "FpgaExchangeTimeInfo.h"

#include <TStopwatch.h>

namespace ml4fpga::fpgacon {
    class FpgaExchangeFactory : public CozyFactory<EmptyConfig>
    {
    public:
        FpgaExchangeFactory() = default;
        void CozyInit() override;
        void CozyProcess(uint64_t run_number, uint64_t event_number) override;

        Input<F125Cluster> m_input_clusters {this};
        Output<FpgaHitsToTrack> m_output_hits_to_track {this};
        Output<FpgaTrackFit> m_output_trak_fit {this};
        Output<FpgaExchangeTimeInfo> m_output_timing {this};

    private:

        size_t m_cfg_fpga_max_hits = 50;   // Max hits to be sent to FPGA
        Parameter<std::string> m_cfg_host {this, "host", "localhost", "Host address to connect to"};
        Parameter<int> m_cfg_port {this, "port", 20250, "Port to connect to"};

        std::unique_ptr<TSocket> m_socket;
    };
}

