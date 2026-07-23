// gemreco stage 6: match X/Y plane peaks into 2-D clusters.
// Faithful port of the legacy ClusterFactory (plugins/gemrecon), including its
// amplitude-assignment quirk (see .cpp).
#pragma once

#include <string>
#include <vector>

#include "../config.hpp"
#include "plane_merge.hpp"

namespace gemreco {

struct Cluster {
    double index_x = 0;
    double index_y = 0;
    double pos_x = 0;   ///< [mm]
    double pos_y = 0;   ///< [mm]
    double energy = 0;
    double amplitude = 0;
};

/// Find clusters for ONE X/Y plane pair (legacy limitation, kept for parity;
/// multi-detector clustering becomes a loop over pairs at the adapter level).
/// Returns empty if either plane is absent or empty in this event.
std::vector<Cluster> match_clusters(const PlanesDecoded& planes, const Config& cfg,
                                    const std::string& plane_x, const std::string& plane_y);

}  // namespace gemreco
