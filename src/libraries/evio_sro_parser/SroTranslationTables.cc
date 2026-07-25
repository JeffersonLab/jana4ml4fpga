#include "SroTranslationTables.h"

#include <sstream>
#include <string>

namespace sro {

namespace {
// Verbatim dac arrays: payload2slot, {ecal,pcal}_rocid2sector, dcrb_rocid2sector,
// dcrb_rocid2region, dcrb_slot2sl, adc{io,layer,strip}ecal_full, adc{layer,strip}pcal_full.
// Table values are 1-based; 0 means "channel not connected".
#include "sro_translation_tables.inc"

constexpr int32_t kSlotCount = 22;      // adc*_full and dcrb_slot2sl are [22][...]
constexpr int32_t kChannelCount = 16;
constexpr int32_t kRocidTableSize = 64; // *_rocid2sector are [64]

// dcrb_rocid2sector/region default to 0 for every non-DCRB rocid, so the tables
// themselves cannot reject a wrong rocid. The instrumented DCRB rocids in this
// cabling are 44-58 (see the table values); anything else is not a drift chamber.
constexpr int32_t kDcrbRocidFirst = 44;
constexpr int32_t kDcrbRocidLast = 58;
} // namespace

int32_t PayloadToSlot(int32_t payload_id) {
    if (payload_id < 1 || payload_id > 16) {
        return 0;
    }
    return payload2slot[payload_id];
}

FadcChannelAddress TranslateFadcChannel(int32_t rocid, int32_t slot, int32_t channel) {
    FadcChannelAddress address;
    if (rocid < 0 || rocid >= kRocidTableSize || slot < 0 || slot >= kSlotCount || channel < 0 || channel >= kChannelCount) {
        return address;
    }

    int32_t ecal_sector = ecal_rocid2sector[rocid];
    if (ecal_sector >= 0) {
        address.detector = Detector::Ecal;
        address.sector = static_cast<int8_t>(ecal_sector);
        address.io = static_cast<int8_t>(adcioecal_full[slot][channel] - 1);
        address.view = static_cast<int8_t>(adclayerecal_full[slot][channel] - 1);
        address.strip = static_cast<int16_t>(adcstripecal_full[slot][channel] - 1);
        return address;
    }

    int32_t pcal_sector = pcal_rocid2sector[rocid];
    if (pcal_sector >= 0) {
        address.detector = Detector::Pcal;
        address.sector = static_cast<int8_t>(pcal_sector);
        address.view = static_cast<int8_t>(adclayerpcal_full[slot][channel] - 1);
        address.strip = static_cast<int16_t>(adcstrippcal_full[slot][channel] - 1);
        return address;
    }

    return address; // rocid in no table (e.g. 29, 84): Detector::Unknown, raw values still stored
}

DcrbBoardAddress TranslateDcrbBoard(int32_t rocid, int32_t slot) {
    DcrbBoardAddress address;
    if (rocid < kDcrbRocidFirst || rocid > kDcrbRocidLast || slot < 0 || slot >= kSlotCount) {
        return address;
    }
    address.sector = static_cast<int8_t>(dcrb_rocid2sector[rocid]);
    address.region = static_cast<int8_t>(dcrb_rocid2region[rocid]);
    address.superlayer = static_cast<int8_t>(dcrb_slot2sl[slot] + address.region * 2);
    return address;
}

void ParseStats::Add(const ParseStats& other) {
    fake_frames += other.fake_frames;
    empty_payload_banks += other.empty_payload_banks;
    unknown_module_words += other.unknown_module_words;
    unknown_rocid_hits += other.unknown_rocid_hits;
    untranslated_hits += other.untranslated_hits;
    structure_errors += other.structure_errors;
}

std::string ParseStats::ToString() const {
    std::ostringstream text;
    text << "fake_frames=" << fake_frames
         << " empty_payload_banks=" << empty_payload_banks
         << " unknown_module_words=" << unknown_module_words
         << " unknown_rocid_hits=" << unknown_rocid_hits
         << " untranslated_hits=" << untranslated_hits
         << " structure_errors=" << structure_errors;
    return text.str();
}

} // namespace sro
