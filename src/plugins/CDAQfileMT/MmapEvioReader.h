// mmap-based EVIO event reader (readout optimization, MT plan).
//
// Replaces the HDEVIO seekg+ifstream-read pattern (measured ~0.63 ms/event
// => ~25 MB/s sequential ceiling) with a memory-mapped walk of the EVIO v4
// structure: the sequential cost per event drops to a length-word walk plus one
// memcpy from the map; actual disk I/O happens via kernel readahead on parallel
// page faults.
//
// The block/event walking logic mirrors HDEVIO::readNoFileBuff + MapEvents:
//   - EVIO block header = 8 words: [0]=block length (words), [1]=block number,
//     [2]=header length (8), [3]=event count, ..., [7]=magic 0xc0da0100
//     (0x0001dac0 when byte-swapped);
//   - events follow contiguously: each is a bank whose word[0] = length
//     (exclusive, i.e. total words - 1);
//   - a trailing 8-word block (words_left == 8) marks EOF (HDEVIO quirk kept).
#pragma once

#include <cstdint>
#include <string>
#include <stdexcept>

#include <rawdataparser/EVIOBlockedEvent.h>

class MmapEvioReader {
public:
    enum class Result { kOk, kEndOfFile, kError };

    explicit MmapEvioReader(const std::string& path);
    ~MmapEvioReader();

    MmapEvioReader(const MmapEvioReader&) = delete;
    MmapEvioReader& operator=(const MmapEvioReader&) = delete;

    /// Copy the next EVIO event into block.data (sets swap_needed/block_number).
    Result NextEvent(EVIOBlockedEvent& block);

    /// Block-batching mode (Dmitry's strategy): copy a SPAN of consecutive events
    /// into block.data as one parallel work unit.
    ///  - max_events == 0: the whole remainder of the current physical EVIO block
    ///    (no event walking on the sequential arrow - just header + one memcpy);
    ///  - max_events > 0: up to that many events (walks event length words).
    /// events_out returns how many events the span contains (from the block header
    /// in whole-block mode). Data is RAW (unswapped); consumer unpacks per bank.
    Result NextSpan(EVIOBlockedEvent& block, uint32_t max_events, uint64_t& events_out);

    /// madvise(WILLNEED) this many MB ahead of the read position on each span
    /// emission (0 = disabled; kernel MADV_SEQUENTIAL readahead still applies).
    void set_prefetch_mb(size_t mb) { m_prefetch_mb = mb; }

    const std::string& path() const { return m_path; }

private:
    static uint32_t ByteSwap32(uint32_t v) { return __builtin_bswap32(v); }
    uint32_t word(uint64_t word_offset, bool swap) const {
        uint32_t v = m_base[word_offset];
        return swap ? ByteSwap32(v) : v;
    }

    std::string m_path;
    int m_fd = -1;
    const uint32_t* m_base = nullptr;  ///< mapped file (word view)
    uint64_t m_size_words = 0;

    /// Parse the block header at m_block_pos, advancing walk state.
    /// Returns kOk with m_events_left/m_event_pos set, or kEndOfFile/kError.
    Result AdvanceToBlockPayload();

    // walk state
    uint64_t m_block_pos = 0;      ///< word offset of the next block header
    uint64_t m_event_pos = 0;      ///< word offset of the next event within the block
    uint32_t m_events_left = 0;    ///< events remaining in the current block
    bool m_swap_needed = false;    ///< current block endianness
    int m_block_number = 1;
    size_t m_prefetch_mb = 0;
};
