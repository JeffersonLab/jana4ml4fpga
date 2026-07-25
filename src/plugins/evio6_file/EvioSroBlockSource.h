// Timeslice-level event source: one JANA timeslice = one evio block (11 frame
// sets in sro_000791). Reads with the naive SroBlockReader and parses the whole
// block in Emit - intentionally single-threaded, unoptimized Phase-I baseline.

#pragma once

#include <memory>
#include <string>

#include <JANA/JEventSource.h>

#include <libraries/evio_sro_parser/SroBlockReader.h>
#include <libraries/evio_sro_parser/SroFrameSetParser.h>

class EvioSroBlockSource : public JEventSource {
public:
    EvioSroBlockSource(std::string resource_name, JApplication* app);

    void Open() override;
    void Close() override;
    Result Emit(JEvent& event) override;

    static std::string GetDescription() { return "SRO evio block source (timeslice level, naive reader)"; }

private:
    std::unique_ptr<sro::SroBlockReader> m_reader;
    sro::RawBlock m_raw_block;    // reused buffer, contents overwritten per Emit
    sro::ParseStats m_run_stats;  // accumulated over all blocks, printed at Close
    uint64_t m_blocks_read = 0;
    bool m_parse_enabled = true;  // evio6_file:parse - off = pure file I/O measurement
};
