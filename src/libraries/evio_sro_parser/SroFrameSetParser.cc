#include "SroFrameSetParser.h"

#include "SroTranslationTables.h"

namespace sro {

namespace {

constexpr uint32_t kSetBankTag = 0xFF60;        // aggregated frame set
constexpr uint32_t kSibBankTag = 0xFF31;        // set info bank (TSS + AIS segments)
constexpr uint32_t kStreamInfoBankTag = 0xFF30; // per-stream info (timestamp + payload map)
constexpr uint32_t kTimestampSegmentTag = 0x31; // inside stream info
constexpr uint32_t kPayloadMapSegmentTag = 0x41;
constexpr int32_t kMaxPayloadId = 16;

struct BankHeader {
    uint32_t content_start; // word index of first content word
    uint32_t content_end;   // one past last content word
    uint32_t next;          // word index right after the bank
    uint32_t tag;
    uint32_t num;
};

// Reads a 2-word bank header at `pos`; returns false if the bank does not fit in
// [pos, limit). All structure walking below funnels through this bounds check.
bool ReadBank(const uint32_t* words, uint32_t pos, uint32_t limit, BankHeader& bank) {
    if (pos + 2 > limit) {
        return false;
    }
    uint32_t exclusive_length = words[pos];
    if (exclusive_length < 1 || pos + 1 + exclusive_length > limit) {
        return false;
    }
    bank.content_start = pos + 2;
    bank.content_end = pos + 1 + exclusive_length;
    bank.next = bank.content_end;
    bank.tag = (words[pos + 1] >> 16) & 0xFFFF;
    bank.num = words[pos + 1] & 0xFF;
    return true;
}

void UnpackFadcPayload(const uint32_t* words, uint32_t data_start, uint32_t data_end,
                       uint32_t frame_index, uint16_t rocid, uint8_t slot, SroBlockData& out) {
    FadcChannelAddress board_address; // translation depends on channel; resolved per hit
    for (uint32_t word_i = data_start; word_i < data_end; word_i++) {
        uint32_t val = words[word_i];
        FadcHit hit;
        hit.frame_index = frame_index;
        hit.rocid = rocid;
        hit.slot = slot;
        hit.channel = static_cast<uint8_t>((val >> 13) & 0xF);
        hit.charge = static_cast<uint16_t>(val & 0x1FFF);
        hit.time_ticks = static_cast<uint16_t>((val >> 17) & 0x3FFF);
        board_address = TranslateFadcChannel(rocid, slot, hit.channel);
        hit.detector = static_cast<int8_t>(board_address.detector);
        hit.sector = board_address.sector;
        hit.io = board_address.io;
        hit.view = board_address.view;
        hit.strip = board_address.strip;
        if (board_address.detector == Detector::Unknown) {
            out.stats.unknown_rocid_hits++;
        } else if (board_address.view < 0 || board_address.strip < 0) {
            out.stats.untranslated_hits++; // table says "channel not connected"
        }
        out.fadc_hits.push_back(hit);
    }
}

void UnpackDcrbPayload(const uint32_t* words, uint32_t data_start, uint32_t data_end,
                       uint32_t frame_index, uint16_t rocid, uint8_t slot, SroBlockData& out) {
    DcrbBoardAddress board_address = TranslateDcrbBoard(rocid, slot);
    // Two words per entry: w0 = chgroup + channel bits 0-28, w1 = channel bits 29-47 + time.
    for (uint32_t word_i = data_start; word_i + 1 < data_end; word_i += 2) {
        uint32_t w0 = words[word_i];
        uint32_t w1 = words[word_i + 1];
        uint32_t chgroup = (w0 >> 29) & 0x7;
        if (chgroup > 1) { // only groups 0 (ch 0-47) and 1 (ch 48-95) exist on a DCRB
            out.stats.structure_errors++;
            continue;
        }
        uint32_t pattern_28_00 = w0 & 0x1FFFFFFF;
        uint32_t pattern_47_29 = w1 & 0x7FFFF;
        uint16_t time_ticks = static_cast<uint16_t>((w1 >> 19) & 0x7FF);
        uint8_t channel_base = static_cast<uint8_t>(chgroup * 48);

        DcrbHit hit;
        hit.frame_index = frame_index;
        hit.rocid = rocid;
        hit.slot = slot;
        hit.time_ticks = time_ticks;
        hit.sector = board_address.sector;
        hit.region = board_address.region;
        hit.superlayer = board_address.superlayer;
        for (uint32_t bit = 0; bit < 29; bit++) {
            if ((pattern_28_00 >> bit) & 0x1) {
                hit.channel = static_cast<uint8_t>(channel_base + bit);
                out.dcrb_hits.push_back(hit);
            }
        }
        for (uint32_t bit = 0; bit < 19; bit++) {
            if ((pattern_47_29 >> bit) & 0x1) {
                hit.channel = static_cast<uint8_t>(channel_base + 29 + bit);
                out.dcrb_hits.push_back(hit);
            }
        }
    }
}

// Parses one ROC time-slice bank (stream info + payload banks). Returns false on
// a structure error; the caller then abandons this ROC bank but not the set.
bool ParseRocBank(const uint32_t* words, const BankHeader& roc_bank, uint32_t frame_index, SroBlockData& out) {
    uint16_t rocid = static_cast<uint16_t>(roc_bank.tag);

    BankHeader stream_info;
    if (!ReadBank(words, roc_bank.content_start, roc_bank.content_end, stream_info) || stream_info.tag != kStreamInfoBankTag) {
        return false;
    }

    // Stream info contents: timestamp segment, then payload-map segment (u16 data).
    uint32_t pos = stream_info.content_start;
    uint32_t ts_header = words[pos];
    uint32_t ts_length = ts_header & 0xFFFF;
    if (((ts_header >> 24) & 0xFF) != kTimestampSegmentTag || ts_length != 3 || pos + 4 > stream_info.content_end) {
        return false;
    }
    pos += 4; // timestamp values are cross-checked at set level via SIB; skip here

    uint32_t map_header = words[pos];
    uint32_t map_words = map_header & 0xFFFF;
    if (map_words == 0) {
        out.stats.fake_frames++; // aggregator gap fill: stream with no payloads
        return true;
    }
    if (((map_header >> 24) & 0xFF) != kPayloadMapSegmentTag || pos + 1 + map_words > stream_info.content_end) {
        return false;
    }
    uint32_t padding_bytes = (map_header >> 22) & 0x3;
    uint32_t entry_count = map_words * 2 - (padding_bytes >> 1);

    // payload id -> module type (0 FADC250, 1 DCRB) and VME slot, from the map.
    int8_t payload_module[kMaxPayloadId + 1];
    uint8_t payload_slot[kMaxPayloadId + 1];
    for (int32_t payload_id = 0; payload_id <= kMaxPayloadId; payload_id++) {
        payload_module[payload_id] = -1;
        payload_slot[payload_id] = 0;
    }
    const uint16_t* map_entries = reinterpret_cast<const uint16_t*>(&words[pos + 1]);
    for (uint32_t entry_i = 0; entry_i < entry_count; entry_i++) {
        uint16_t entry = map_entries[entry_i];
        int32_t payload_id = entry & 0x1F;
        if (payload_id < 1 || payload_id > kMaxPayloadId) {
            out.stats.structure_errors++;
            continue;
        }
        payload_module[payload_id] = static_cast<int8_t>((entry >> 8) & 0xF);
        payload_slot[payload_id] = static_cast<uint8_t>(PayloadToSlot(payload_id));
    }

    // Payload banks follow the stream info bank inside the ROC bank.
    uint32_t bank_pos = stream_info.next;
    while (bank_pos < roc_bank.content_end) {
        BankHeader payload_bank;
        if (!ReadBank(words, bank_pos, roc_bank.content_end, payload_bank)) {
            return false;
        }
        bank_pos = payload_bank.next;

        if (payload_bank.content_start == payload_bank.content_end) {
            out.stats.empty_payload_banks++;
            continue;
        }
        int32_t payload_id = static_cast<int32_t>(payload_bank.tag);
        int8_t module = (payload_id >= 1 && payload_id <= kMaxPayloadId) ? payload_module[payload_id] : int8_t{-1};
        if (module == 0) {
            UnpackFadcPayload(words, payload_bank.content_start, payload_bank.content_end,
                              frame_index, rocid, payload_slot[payload_id], out);
        } else if (module == 1) {
            UnpackDcrbPayload(words, payload_bank.content_start, payload_bank.content_end,
                              frame_index, rocid, payload_slot[payload_id], out);
        } else {
            out.stats.unknown_module_words += payload_bank.content_end - payload_bank.content_start;
        }
    }
    return true;
}

} // namespace

uint32_t ParseBlockBody(const uint32_t* words, size_t word_count, uint32_t expected_sets, SroBlockData& out) {
    out.frames.clear();
    out.fadc_hits.clear();
    out.dcrb_hits.clear();

    uint32_t limit = static_cast<uint32_t>(word_count);
    uint32_t pos = 0;
    while (pos < limit) {
        BankHeader set_bank;
        if (!ReadBank(words, pos, limit, set_bank) || set_bank.tag != kSetBankTag) {
            out.stats.structure_errors++;
            break; // cannot even delimit the set - abandon the rest of the block
        }
        pos = set_bank.next;

        // SIB carries the set-level frame number and timestamp. The AIS segment
        // after the timestamp is ignored (degenerate in real data).
        BankHeader sib;
        uint32_t frame_number = 0;
        uint64_t timestamp = 0;
        if (ReadBank(words, set_bank.content_start, set_bank.content_end, sib) && sib.tag == kSibBankTag
            && sib.content_start + 4 <= sib.content_end) {
            frame_number = words[sib.content_start + 1];
            timestamp = (static_cast<uint64_t>(words[sib.content_start + 3]) << 32) | words[sib.content_start + 2];
        } else {
            out.stats.structure_errors++;
            continue;
        }

        uint32_t frame_index = static_cast<uint32_t>(out.frames.size());
        FrameInfo frame;
        frame.frame_number = frame_number;
        frame.timestamp = timestamp;
        frame.first_fadc_hit = static_cast<uint32_t>(out.fadc_hits.size());
        frame.first_dcrb_hit = static_cast<uint32_t>(out.dcrb_hits.size());

        uint32_t roc_pos = sib.next;
        while (roc_pos < set_bank.content_end) {
            BankHeader roc_bank;
            if (!ReadBank(words, roc_pos, set_bank.content_end, roc_bank)) {
                out.stats.structure_errors++;
                break;
            }
            roc_pos = roc_bank.next;
            if (!ParseRocBank(words, roc_bank, frame_index, out)) {
                out.stats.structure_errors++;
            }
        }

        frame.fadc_hit_count = static_cast<uint32_t>(out.fadc_hits.size()) - frame.first_fadc_hit;
        frame.dcrb_hit_count = static_cast<uint32_t>(out.dcrb_hits.size()) - frame.first_dcrb_hit;
        out.frames.push_back(frame);
    }

    if (out.frames.size() != expected_sets) {
        out.stats.structure_errors++;
    }
    return static_cast<uint32_t>(out.frames.size());
}

} // namespace sro
