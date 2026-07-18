// gemreco stage 4: merge per-APV decoded data into per-plane strip arrays.
// Faithful port of the legacy PlaneDecodedDataFactory (plugins/gemrecon),
// including its zero-substitution for missing/malformed APVs.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "../config.hpp"
#include "apv_decode.hpp"

namespace gemreco {

struct Pedestals;  // calibration.hpp

/// One plane's merged data: strip values per time bin + merged pedestal noises.
/// data[timebin][strip], strips ordered by the plane's APV list (config order).
struct PlaneDecoded {
    std::vector<std::vector<double>> data;
    std::vector<double> noises;
};

using PlanesDecoded = std::map<std::string, PlaneDecoded>;

/// Merge decoded APVs into planes. Every configured plane gets an entry; planes
/// with no data in this event get an empty record (legacy behavior, keeps
/// downstream lookups from throwing). APVs missing from `decoded` or with a
/// mismatched time-bin count are substituted with zeros.
PlanesDecoded merge_planes(const std::map<int, ApvDecoded>& decoded,
                           const Pedestals& pedestals, const Config& cfg);

}  // namespace gemreco
