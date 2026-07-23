//
// Created by romanov on 7/23/2024.
// Subject to the terms in the LICENSE file found in the top-level directory.
//

#pragma once

namespace ml4fpga::fpgacon {

    struct FpgaExchangeTimeInfo {
        double event_cpu_time = 0;
        double event_real_time = 0;
        double send_cpu_time = 0;
        double send_real_time = 0;
        double receive1_cpu_time = 0;
        double receive1_real_time = 0;
        double receive2_cpu_time = 0;
        double receive2_real_time = 0;
    };
}