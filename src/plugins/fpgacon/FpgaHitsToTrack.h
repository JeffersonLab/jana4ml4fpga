//
// Created by Dmitry Romanov on 3/8/2024.
// Subject to the terms in the LICENSE file found in the top-level directory.
//

#pragma once


namespace ml4fpga::fpgacon {
    struct FpgaHitsToTrack {
        int hit_index=0;        /// Index of the hit (cluster)
        int track_index=0;        /// Index of the track associated with the hit
    };
}
