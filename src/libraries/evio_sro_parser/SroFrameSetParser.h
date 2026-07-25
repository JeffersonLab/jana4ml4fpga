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
#include <vector>

#include "SroData.h"

namespace sro {

/// Parses all frame sets of one block body into `out` (frames + hits appended to
/// cleared vectors, ParseStats accumulated). `expected_sets` comes from the block
/// header event_count; a mismatch counts as a structure error.
/// Returns the number of frame sets parsed.
uint32_t ParseBlockBody(const uint32_t* words, size_t word_count, uint32_t expected_sets, SroBlockData& out);

/// Lazy variant for the filtered chain: decodes only ECAL ROC banks (the finder
/// input) and records every other ROC bank as a DeferredRocBank word range.
/// `words` must equal out.Body() and stay valid until deferred decoding is done
/// (owned body_words, or a mapping pinned by out.body_owner). Sets out.lazy = true.
uint32_t ParseBlockBodyLazy(const uint32_t* words, size_t word_count, uint32_t expected_sets, SroBlockData& out);

/// Decodes the deferred ROC banks of one frame of a lazily parsed block,
/// appending to the given vectors (hit frame_index is the block-level index).
/// Together with the block's ECAL hit slice this reproduces exactly the hits
/// eager parsing yields for the frame - values and counts, not order.
void DecodeDeferredFrame(const SroBlockData& block, uint32_t frame_index,
                         std::vector<FadcHit>& fadc_out, std::vector<DcrbHit>& dcrb_out,
                         ParseStats& stats);

} // namespace sro
