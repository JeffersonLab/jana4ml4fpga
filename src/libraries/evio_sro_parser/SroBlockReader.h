// Naive sequential reader of SRO evio files (the intentional Phase-I baseline:
// plain fread, one thread, no mmap, no readahead - future readers get compared
// against this).
//
// File layout (measured, see space/notes/data-format-observed.md): a sequence of
// blocks, each an 8-word header {len, block#, hdr_len=8, event_count, 0, version,
// 0, magic 0xc0da0100} followed by (len-8) body words holding `event_count`
// aggregated frame sets. Little-endian only; a byte-swapped magic is an error,
// not a supported case.

#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "SroData.h"

namespace sro {

/// One evio block as read from disk: header fields + body words (header stripped).
/// The words vector is reused across ReadNextBlock calls - hold no references
/// across calls.
struct RawBlock {
    uint32_t block_number = 0;
    uint32_t event_count = 0;
    std::string source_file;
    std::vector<uint32_t> words;
};

class SroBlockReader {
public:
    /// Files are read back to back in the given order. Frame order across files
    /// does not matter in Phase I (files were written round-robin).
    explicit SroBlockReader(std::vector<std::string> file_paths);
    ~SroBlockReader();

    SroBlockReader(const SroBlockReader&) = delete;
    SroBlockReader& operator=(const SroBlockReader&) = delete;

    /// Reads the next block into `block` (contents overwritten). Returns false
    /// when all files are exhausted. A truncated block at the end of a file is
    /// expected (the DAQ writer gets cut off mid-block when a run stops;
    /// sro_000791.evio.00000 ends this way): it is counted, the rest of that
    /// file is skipped, and reading continues with the next file. Malformed
    /// headers before EOF still throw std::runtime_error.
    bool ReadNextBlock(RawBlock& block);

    /// Number of partial tail blocks dropped so far (log this at end of run).
    uint64_t TruncatedTailBlocks() const { return m_truncated_tail_blocks; }

private:
    bool OpenNextFile();

    std::vector<std::string> m_file_paths;
    size_t m_next_file_index = 0;
    std::FILE* m_file = nullptr;
    std::string m_current_file;
    uint64_t m_truncated_tail_blocks = 0;
};

} // namespace sro
