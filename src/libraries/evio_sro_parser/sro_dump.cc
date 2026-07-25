// sro_dump - standalone decoder check for SRO evio files (no JANA, no ROOT).
//
// Reads N blocks, parses every frame set, and prints summaries meant for
// eyeball-validation against the raw bytes and dac reference dumps:
//   - per-block: frames, hit counts, parse anomalies
//   - aggregate: per-rocid and per-detector hit counts, DCRB full-mask fraction
//   - first FADC hits with raw and translated fields
//
// Usage: sro_dump <file.evio> [more files...] [-n blocks] [-v]

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "SroBlockReader.h"
#include "SroFrameSetParser.h"

namespace {

const char* DetectorName(int8_t detector) {
    switch (detector) {
        case 0: return "ECAL";
        case 1: return "PCAL";
        default: return "UNKNOWN";
    }
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> files;
    int64_t max_blocks = 10;
    bool verbose = false;
    for (int arg_i = 1; arg_i < argc; arg_i++) {
        if (std::strcmp(argv[arg_i], "-n") == 0 && arg_i + 1 < argc) {
            max_blocks = std::atoll(argv[++arg_i]);
        } else if (std::strcmp(argv[arg_i], "-v") == 0) {
            verbose = true;
        } else {
            files.push_back(argv[arg_i]);
        }
    }
    if (files.empty()) {
        std::fprintf(stderr, "usage: sro_dump <file.evio> [more files...] [-n blocks] [-v]\n");
        return 1;
    }

    sro::SroBlockReader reader(files);
    sro::RawBlock raw;
    sro::SroBlockData parsed;
    sro::ParseStats total_stats;

    uint64_t total_frames = 0;
    uint64_t total_fadc = 0;
    uint64_t total_dcrb = 0;
    uint32_t first_frame = 0;
    uint32_t last_frame = 0;
    std::map<uint16_t, uint64_t> fadc_per_rocid;
    std::map<uint16_t, uint64_t> dcrb_per_rocid;
    std::map<int8_t, uint64_t> fadc_per_detector;
    // (rocid, slot, chgroup, time) -> set bits; 48 set bits = a full-mask word pair
    std::map<uint64_t, uint32_t> dcrb_mask_population;
    uint64_t sample_hits_printed = 0;

    for (int64_t block_i = 0; block_i < max_blocks && reader.ReadNextBlock(raw); block_i++) {
        parsed.stats = sro::ParseStats();
        uint32_t set_count = sro::ParseBlockBody(raw.words.data(), raw.words.size(), raw.event_count, parsed);
        total_stats.Add(parsed.stats);
        total_frames += set_count;
        total_fadc += parsed.fadc_hits.size();
        total_dcrb += parsed.dcrb_hits.size();
        if (block_i == 0 && !parsed.frames.empty()) {
            first_frame = parsed.frames.front().frame_number;
        }
        if (!parsed.frames.empty()) {
            last_frame = parsed.frames.back().frame_number;
        }

        for (const sro::FadcHit& hit : parsed.fadc_hits) {
            fadc_per_rocid[hit.rocid]++;
            fadc_per_detector[hit.detector]++;
            if (sample_hits_printed < 15 && hit.detector == 0) {
                std::printf("  sample ECAL hit: roc=%2u slot=%2u ch=%2u q=%4u t=%5u -> sec=%d io=%d view=%d strip=%d\n",
                            hit.rocid, hit.slot, hit.channel, hit.charge, hit.time_ticks,
                            hit.sector, hit.io, hit.view, hit.strip);
                sample_hits_printed++;
            }
        }
        for (const sro::DcrbHit& hit : parsed.dcrb_hits) {
            dcrb_per_rocid[hit.rocid]++;
            uint64_t mask_key = (static_cast<uint64_t>(hit.frame_index) << 40)
                              | (static_cast<uint64_t>(hit.rocid) << 24)
                              | (static_cast<uint64_t>(hit.slot) << 16)
                              | (static_cast<uint64_t>(hit.channel / 48) << 12)
                              | hit.time_ticks;
            dcrb_mask_population[mask_key]++;
        }

        if (verbose) {
            std::printf("block %u: sets=%u fadc_hits=%zu dcrb_hits=%zu frames %u..%u | %s\n",
                        raw.block_number, set_count, parsed.fadc_hits.size(), parsed.dcrb_hits.size(),
                        parsed.frames.empty() ? 0 : parsed.frames.front().frame_number,
                        parsed.frames.empty() ? 0 : parsed.frames.back().frame_number,
                        parsed.stats.ToString().c_str());
        }
    }

    uint64_t full_masks = 0;
    for (const auto& [key, population] : dcrb_mask_population) {
        if (population == 48) {
            full_masks++;
        }
    }

    std::printf("\n=== totals ===\n");
    std::printf("frames=%" PRIu64 " (numbers %u..%u)  fadc_hits=%" PRIu64 "  dcrb_hits=%" PRIu64 "\n",
                total_frames, first_frame, last_frame, total_fadc, total_dcrb);
    std::printf("parse stats: %s\n", total_stats.ToString().c_str());
    std::printf("\nFADC hits per rocid:\n");
    for (const auto& [rocid, count] : fadc_per_rocid) {
        std::printf("  roc %3u : %8" PRIu64 "\n", rocid, count);
    }
    std::printf("FADC hits per detector:\n");
    for (const auto& [detector, count] : fadc_per_detector) {
        std::printf("  %-7s : %8" PRIu64 "\n", DetectorName(detector), count);
    }
    std::printf("DCRB hits per rocid:\n");
    for (const auto& [rocid, count] : dcrb_per_rocid) {
        std::printf("  roc %3u : %8" PRIu64 "\n", rocid, count);
    }
    std::printf("DCRB mask word-pairs: %zu total, %" PRIu64 " with all 48 channels set (%.1f%% of hits come from full masks)\n",
                dcrb_mask_population.size(), full_masks,
                total_dcrb > 0 ? 100.0 * (full_masks * 48.0) / total_dcrb : 0.0);
    return 0;
}
