// Parser of aggregated SRO frame sets (the payload of one evio block) into flat
// hit tables. Decode logic ported from dac's sroLib.c sroEventBuilder pass 1;
// see space/notes/data-format-observed.md for the measured wire format.
//
// Differences from dac, on purpose:
//  - module_id is taken per payload from the stream payload map (dac reused the
//    last payload's module_id for the whole stream - works only because streams
//    are module-homogeneous);
//  - the set-level AIS is ignored (degenerate in real data);
//  - format surprises increment ParseStats counters instead of calling exit().

#pragma once

#include <cstdint>

#include "SroData.h"

namespace sro {

/// Parses all frame sets of one block body into `out` (frames + hits appended to
/// cleared vectors, ParseStats accumulated). `expected_sets` comes from the block
/// header event_count; a mismatch counts as a structure error.
/// Returns the number of frame sets parsed.
uint32_t ParseBlockBody(const uint32_t* words, size_t word_count, uint32_t expected_sets, SroBlockData& out);

} // namespace sro
