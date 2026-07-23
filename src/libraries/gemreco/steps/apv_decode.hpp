// gemreco stage 3: APV25 ADC decoding - pedestal offset + common-mode correction.
// Faithful port of the legacy ApvDecodedDataFactory::DecodeApv (plugins/gemrecon),
// including two legacy quirks preserved for bit-for-bit parity (see .cpp).
#pragma once

#include <vector>

#include "../config.hpp"
#include "decode_apv.hpp"

namespace gemreco {

struct ApvPedestal;  // calibration.hpp

/// Decoded ADC data of one APV.
struct ApvDecoded {
    /// data[timebin][channel] = pedestal- and common-mode-corrected, sign-flipped ADC
    std::vector<std::vector<double>> data;
    std::vector<double> common_mode_offsets;  ///< per timebin
    std::vector<double> raw_data_average;     ///< legacy "rawDataZS" (see quirk note in .cpp)
};

/// Decode one APV block against the given pedestal. Returns empty ApvDecoded for
/// malformed input (legacy: such APVs are skipped and later zero-substituted).
ApvDecoded decode_apv_adc(const ApvRawBlock& block, const ApvPedestal& pedestal,
                          const AlgoParams& params);

}  // namespace gemreco
