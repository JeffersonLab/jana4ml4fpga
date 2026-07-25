// Cabling translation for the CLAS12 SRO test stand (run sro_000791).
//
// Wraps the verbatim dac tables (sro_translation_tables.inc) behind bounds-checked
// lookups. dac indexed tables with unchecked values and could read [-1]; here any
// out-of-table input yields Detector::Unknown / -1 fields instead.

#pragma once

#include <cstdint>

#include "SroData.h"

namespace sro {

/// Translated address of one FADC channel. Fields are -1 where no table applies.
struct FadcChannelAddress {
    Detector detector = Detector::Unknown;
    int8_t sector = -1;
    int8_t io = -1;      ///< ECAL only: 0=inner 1=outer
    int8_t view = -1;    ///< 0=U 1=V 2=W
    int16_t strip = -1;  ///< 0-based
};

/// Translated address of one DCRB board. Channel-to-wire mapping is not applied
/// in Phase I (see space/notes/DECISIONS.md #8).
struct DcrbBoardAddress {
    int8_t sector = -1;
    int8_t region = -1;
    int8_t superlayer = -1;  ///< 0-5
};

/// payload id (1-16, from the stream payload map) -> physical VME slot; 0 on bad input.
int32_t PayloadToSlot(int32_t payload_id);

/// FADC channel -> detector address. rocids 29 and 84 are in no table and come back
/// Unknown on purpose (identity unresolved, see phase1_QUESTIONS.md Q11).
FadcChannelAddress TranslateFadcChannel(int32_t rocid, int32_t slot, int32_t channel);

/// DCRB board -> sector/region/superlayer; fields -1 if rocid is not a DCRB rocid.
DcrbBoardAddress TranslateDcrbBoard(int32_t rocid, int32_t slot);

} // namespace sro
