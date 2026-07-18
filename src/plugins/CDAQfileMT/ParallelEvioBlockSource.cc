#include "ParallelEvioBlockSource.h"

#include <algorithm>
#include <JANA/JApplication.h>
#include <rawdataparser/EVIOBlockedEventParser.h>
#include <rawdataparser/swap_bank.h>

ParallelEvioBlockSource::~ParallelEvioBlockSource() {
    if (m_hdevio) CloseEVIOFile();
}

void ParallelEvioBlockSource::Initialize() {
    LOG_INFO(m_logger) << "Initializing ParallelEvioBlockSource ("
                       << m_filenames.size() << " file(s))" << LOG_END;

    // Same parser knob as the serial CDAQEVIOFileSource - required for parity
    japp->SetDefaultParameter("daq:srs_window_raw:ntsamples", m_parser_config.NSAMPLES_GEMSRS,
                              "Number of GEM SRS time samples per APV");

    // jana:nevents is enforced by the standard JEventSourceArrow, which the block
    // topology bypasses - honor it here (block granularity: typically 1 event/block,
    // control blocks may cause a small overshoot).
    japp->SetDefaultParameter("jana:nevents", m_nevents_limit,
                              "Max number of events that sources can emit");

    // Reader backend:
    //  "hdevio"     - seekg+ifstream per event (legacy pattern)
    //  "mmap"       - memory-mapped walk, one EVENT per work unit
    //  "mmap_block" - memory-mapped, one physical EVIO BLOCK (or evio:events_per_item
    //                 events) per work unit; amortizes JANA per-item costs ~1000x.
    //                 Event ORDER IS NOT PRESERVED across work units.
    std::string reader = "hdevio";
    japp->SetDefaultParameter("evio:reader", reader,
                              "Parallel EVIO reader backend: 'hdevio', 'mmap' or 'mmap_block'");
    if (reader == "mmap") m_reader = Reader::kMmap;
    else if (reader == "mmap_block") m_reader = Reader::kMmapBlock;
    else m_reader = Reader::kHdevio;

    uint32_t events_per_item = 512;  // same default as CDAQfileMT.cc (topology tuning)
    japp->SetDefaultParameter("evio:events_per_item", events_per_item,
                              "mmap_block mode: events per parallel work item "
                              "(0 = one whole physical EVIO block per item)");
    m_events_per_item = events_per_item;

    size_t prefetch_mb = 256;
    japp->SetDefaultParameter("evio:prefetch_mb", prefetch_mb,
                              "mmap modes: madvise(WILLNEED) read-ahead depth in MB (0 = off)");
    m_prefetch_mb = prefetch_mb;

    OpenNextEVIOFile();
}

void ParallelEvioBlockSource::OpenNextEVIOFile() {
    if (m_filenames.empty()) {
        throw JException("ParallelEvioBlockSource: no EVIO files to open");
    }
    m_cur_file = m_filenames.back();
    m_filenames.pop_back();

    if (m_reader != Reader::kHdevio) {
        m_mmap = std::make_unique<MmapEvioReader>(m_cur_file);
        m_mmap->set_prefetch_mb(m_prefetch_mb);
        LOG_INFO(m_logger) << "Opened EVIO file (mmap reader) \"" << m_cur_file << "\"" << LOG_END;
        return;
    }

    m_hdevio = std::make_unique<HDEVIO>(m_cur_file, true, 2);  // 2 = VERBOSE level
    if (!m_hdevio->is_open) {
        LOG_ERROR(m_logger) << m_hdevio->err_mess.str() << LOG_END;
        throw JException("Failed to open EVIO file: " + m_cur_file);
    }
    LOG_INFO(m_logger) << "Opened EVIO file \"" << m_cur_file << "\"" << LOG_END;
}

void ParallelEvioBlockSource::CloseEVIOFile() {
    if (m_mmap) {
        m_mmap.reset();
    }
    if (m_hdevio) {
        m_hdevio->PrintStats();
        m_hdevio.reset();
    }
    LOG_INFO(m_logger) << "Closed EVIO file \"" << m_cur_file << "\"" << LOG_END;
}

JBlockedEventSource<EVIOBlockedEvent>::Status
ParallelEvioBlockSource::NextBlock(EVIOBlockedEvent& block) {
    // jana:nevents enforcement (see Initialize). Block/span mode may overshoot by
    // up to one work item.
    uint64_t emitted = (m_reader == Reader::kHdevio || m_reader == Reader::kMmap)
                           ? static_cast<uint64_t>(m_block_number)
                           : m_events_emitted;
    if (m_nevents_limit > 0 && emitted > m_nevents_limit) {
        LOG_INFO(m_logger) << "Reached jana:nevents=" << m_nevents_limit << " - stopping" << LOG_END;
        // Stop(false) = request_stop: shuts sources off, DRAINS all queues, then the
        // main Run() loop joins workers and finalizes processors (unlike Quit(true),
        // which skips the join and thereby skips JEventProcessor::Finish).
        japp->Stop(false);
        return Status::FailFinished;
    }

    // SEQUENTIAL SECTION: keep this as cheap as possible - read + copy only.
    if (m_reader != Reader::kHdevio) {
        while (true) {
            MmapEvioReader::Result result;
            if (m_reader == Reader::kMmapBlock) {
                uint64_t events_in_span = 0;
                result = m_mmap->NextSpan(block, m_events_per_item, events_in_span);
                m_events_emitted += events_in_span;
            } else {
                result = m_mmap->NextEvent(block);
            }
            if (result == MmapEvioReader::Result::kOk) {
                block.block_number = m_block_number++;  // keep global numbering across files
                return Status::Success;
            }
            if (result == MmapEvioReader::Result::kError) {
                throw JException("MmapEvioReader: corrupt EVIO structure in " + m_cur_file);
            }
            // kEndOfFile
            CloseEVIOFile();
            if (m_filenames.empty()) {
                japp->Stop(false);  // graceful: drain queues, finalize processors
                return Status::FailFinished;
            }
            OpenNextEVIOFile();
        }
    }

    bool read_ok = m_hdevio->readNoFileBuff(m_buff.data(), m_buff.size());
    uint32_t cur_len = m_hdevio->last_event_len;

    if (read_ok) {
        block.block_number = m_block_number++;
        block.swap_needed = m_hdevio->swap_needed;
        block.data.assign(m_buff.begin(), m_buff.begin() + cur_len);
        return Status::Success;
    }

    if (m_hdevio->err_code == HDEVIO::HDEVIO_USER_BUFFER_TOO_SMALL) {
        m_buff.resize(cur_len);
        return Status::FailTryAgain;
    }

    if (m_hdevio->err_code == HDEVIO::HDEVIO_EOF) {
        CloseEVIOFile();
        if (m_filenames.empty()) {
            // JANA 2.1.x does not honor FailFinished from block sources as an end
            // condition (prototype FIXME) - request a graceful stop: queues drain,
            // workers join, processors finalize (the prototype's Quit(true) skipped
            // finalization and corrupted ROOT output).
            japp->Stop(false);
            return Status::FailFinished;
        }
        OpenNextEVIOFile();
        return Status::FailTryAgain;
    }

    throw JException("ParallelEvioBlockSource: unhandled HDEVIO read status %d",
                     m_hdevio->err_code);
}

std::vector<std::shared_ptr<JEvent>>
ParallelEvioBlockSource::DisentangleBlock(EVIOBlockedEvent& block, JEventPool& pool) {
    // PARALLEL SECTION: runs concurrently on worker threads (JBlockDisentanglerArrow
    // is parallel). Local parser per call - no shared mutable state.
    //
    // Endianness: HDEVIO's readNoFileBuff pre-swaps banks in place before the parser
    // ever sees the data (while still flagging swap_needed=true - the parser relies
    // on native words). The mmap paths deliver RAW data, so the structural swap
    // happens HERE, on the parallel arrow.

    if (m_reader == Reader::kMmapBlock) {
        // Block-batching mode: block.data holds MANY consecutive top-level event
        // banks (the parser handles exactly one per call). Unpack bank by bank;
        // event ORDER across work units is NOT preserved by design.
        std::vector<std::shared_ptr<JEvent>> all_events;
        EVIOBlockedEventParser parser;
        parser.Configure(m_parser_config);

        EVIOBlockedEvent sub;
        sub.swap_needed = block.swap_needed;

        const uint32_t* data = block.data.data();
        const size_t size = block.data.size();
        size_t pos = 0;
        while (pos < size) {
            uint32_t len_word = data[pos];
            if (block.swap_needed) len_word = __builtin_bswap32(len_word);
            const size_t event_len = static_cast<size_t>(len_word) + 1;
            if (pos + event_len > size) {
                throw JException("ParallelEvioBlockSource: event bank crosses span boundary");
            }

            sub.block_number = block.block_number;
            sub.data.resize(event_len);
            if (block.swap_needed) {
                // swap+copy in one pass (structural, swap-type-aware)
                swap_bank(sub.data.data(), const_cast<uint32_t*>(data + pos),
                          static_cast<uint32_t>(event_len));
            } else {
                std::copy(data + pos, data + pos + event_len, sub.data.begin());
            }

            auto events = parser.ParseEVIOBlockedEvent(sub, pool);
            all_events.insert(all_events.end(), events.begin(), events.end());
            pos += event_len;
        }
        return all_events;
    }

    if (m_reader == Reader::kMmap && block.swap_needed && !block.data.empty()) {
        swap_bank(block.data.data(), block.data.data(),
                  static_cast<uint32_t>(block.data.size()));
    }

    EVIOBlockedEventParser parser;
    parser.Configure(m_parser_config);
    return parser.ParseEVIOBlockedEvent(block, pool);
}
