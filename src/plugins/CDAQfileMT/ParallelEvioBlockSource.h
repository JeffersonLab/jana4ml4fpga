// Parallel EVIO file source (MT plan: break the sequential-source ~154 Hz wall).
//
// Copied/cleaned from src/executables/jana4ml4fpga/EVIOBlockedEventFileSource
// (the -t topology prototype). JANA2 mechanism:
//   JBlockSourceArrow      (sequential) -> NextBlock():        cheap raw block read
//   JBlockDisentanglerArrow (PARALLEL)  -> DisentangleBlock(): expensive EVIO parse
// so the per-event cost on the sequential arrow drops to the file read + memcpy,
// and parsing scales with worker threads.
//
// Differences vs the prototype (all deliberate):
//  - no per-event std::cout spam in DisentangleBlock (was a serialization point)
//  - parser is configured from daq:srs_window_raw:ntsamples exactly like the
//    serial CDAQEVIOFileSource (required for output parity)
//  - guarded destructor (no crash when no file was ever opened)
//  - reusable read buffer instead of new/delete per block
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <JANA/JBlockedEventSource.h>
#include <JANA/JLogger.h>

#include <evio/HDEVIO.h>
#include <rawdataparser/EVIOBlockedEvent.h>
#include <rawdataparser/EVIOBlockedEventParserConfig.h>

#include "MmapEvioReader.h"

class ParallelEvioBlockSource : public JBlockedEventSource<EVIOBlockedEvent> {
public:
    explicit ParallelEvioBlockSource(std::vector<std::string> filenames)
        : m_filenames(std::move(filenames)) {}

    ~ParallelEvioBlockSource();  // JBlockedEventSource has a non-virtual dtor in this JANA version

    void Initialize() override;

    /// Sequential arrow: read ONE raw EVIO block (cheap: I/O + memcpy).
    Status NextBlock(EVIOBlockedEvent& block) override;

    /// Parallel arrow: parse the block into JEvents (expensive; thread-safe -
    /// local parser per call, no shared mutable state).
    std::vector<std::shared_ptr<JEvent>> DisentangleBlock(EVIOBlockedEvent& block,
                                                          JEventPool& pool) override;

private:
    void OpenNextEVIOFile();
    void CloseEVIOFile();

    static constexpr uint32_t kDefaultBuffLen = 4000000;

    enum class Reader { kHdevio, kMmap, kMmapBlock };

    int m_block_number = 1;
    uint64_t m_nevents_limit = 0;   ///< jana:nevents (0 = unlimited)
    uint64_t m_events_emitted = 0;  ///< events handed out so far (nevents accounting)
    Reader m_reader = Reader::kHdevio;
    uint32_t m_events_per_item = 0; ///< evio:events_per_item (0 = whole physical block)
    size_t m_prefetch_mb = 256;     ///< evio:prefetch_mb
    JLogger m_logger;

    std::vector<std::string> m_filenames;  ///< remaining files (back = next)
    std::string m_cur_file;
    std::unique_ptr<HDEVIO> m_hdevio;          // reader=hdevio
    std::unique_ptr<MmapEvioReader> m_mmap;    // reader=mmap

    std::vector<uint32_t> m_buff = std::vector<uint32_t>(kDefaultBuffLen);
    EVIOBlockedEventParserConfig m_parser_config;
};
