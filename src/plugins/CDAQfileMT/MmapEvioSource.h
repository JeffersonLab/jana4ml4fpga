// Parallel EVIO file source for JANA2 2026.x (migration of the old block-arrow
// design - see ai_rework/reports/phase1_block_batching.md for its history).
//
// New-architecture mapping (no custom topology needed):
//   Emit()            SEQUENTIAL source arrow: mmap walk + one memcpy of the raw
//                     event bank into the JEvent (as an inserted EVIOBlockedEvent).
//   ProcessParallel() PARALLEL map arrow (EnableProcessParallel): structural
//                     byte-swap + full EVIO parse into the same JEvent.
//
// The expensive deserialization therefore scales with worker threads while the
// sequential cost per event is a ~16 kB memcpy, exactly like the measured-good
// mmap design on JANA 2.1.x.
#pragma once

#include <memory>

#include <JANA/JEventSource.h>
#include <JANA/JEventSourceGeneratorT.h>
#include <spdlog/logger.h>

#include <rawdataparser/EVIOBlockedEventParserConfig.h>

#include "MmapEvioReader.h"

class MmapEvioSource : public JEventSource {
public:
    MmapEvioSource(std::string resource_name, JApplication* app);

    void Open() override;
    void Close() override;

    /// Sequential: hand one raw (unswapped) EVIO event bank to the JEvent.
    Result Emit(JEvent& event) override;

    /// Parallel (map arrow): swap + parse the raw bank into this event.
    /// `this` is const by API contract - all mutable state lives in the JEvent.
    void ProcessParallel(JEvent& event) const override;

    static std::string GetDescription();

private:
    std::shared_ptr<spdlog::logger> m_log;   // aspect logger: "evio"
    std::unique_ptr<MmapEvioReader> m_reader;
    EVIOBlockedEventParserConfig m_parser_config;
    size_t m_prefetch_mb = 256;
};

template <>
double JEventSourceGeneratorT<MmapEvioSource>::CheckOpenable(std::string);
