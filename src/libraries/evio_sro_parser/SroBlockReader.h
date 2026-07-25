// Sequential reader of SRO evio files with two modes:
//   fread (Phase-I baseline) - copies each block body into an owned buffer;
//   mmap  - maps whole files read-only and returns pointers into the mapping
//           (MADV_SEQUENTIAL). Zero copy; a block's mapping stays valid while
//           any RawBlock/consumer holds the shared_ptr to it.
//
// File layout (measured, see space/notes/data-format-observed.md): a sequence of
// blocks, each an 8-word header {len, block#, hdr_len=8, event_count, 0, version,
// 0, magic 0xc0da0100} followed by (len-8) body words holding `event_count`
// aggregated frame sets. Little-endian only; a byte-swapped magic is an error,
// not a supported case.

#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "SroData.h"

namespace sro {

/// One input file mapped read-only. Destructor unmaps, so consumers keep the
/// shared_ptr for as long as they dereference words inside the mapping.
class MappedFile {
public:
    explicit MappedFile(const std::string& path); // throws std::runtime_error
    ~MappedFile();
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    const uint32_t* Words() const { return static_cast<const uint32_t*>(m_base); }
    size_t WordCount() const { return m_bytes / sizeof(uint32_t); }

private:
    void* m_base = nullptr;
    size_t m_bytes = 0;
};

/// One evio block as read from disk: header fields + body words (header stripped).
/// `body`/`body_word_count` are valid in both reader modes. In fread mode they
/// point into `words` (reused per ReadNextBlock call - hold no references across
/// calls, or take the vector). In mmap mode they point into `mapping`, which
/// keeps the words alive for whoever copies the shared_ptr.
struct RawBlock {
    uint32_t block_number = 0;
    uint32_t event_count = 0;
    std::string source_file;
    std::vector<uint32_t> words;
    const uint32_t* body = nullptr;
    size_t body_word_count = 0;
    std::shared_ptr<const MappedFile> mapping;
};

class SroBlockReader {
public:
    /// Files are read back to back in the given order. Frame order across files
    /// does not matter in Phase I (files were written round-robin).
    explicit SroBlockReader(std::vector<std::string> file_paths, bool use_mmap = false);
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
    bool ReadNextBlockFread(RawBlock& block);
    bool ReadNextBlockMmap(RawBlock& block);
    bool ValidateHeader(const uint32_t* header) const;

    std::vector<std::string> m_file_paths;
    size_t m_next_file_index = 0;
    bool m_use_mmap = false;
    std::FILE* m_file = nullptr;                    // fread mode
    std::shared_ptr<const MappedFile> m_mapped;     // mmap mode: current file
    size_t m_map_pos = 0;                           // mmap mode: word cursor
    std::string m_current_file;
    uint64_t m_truncated_tail_blocks = 0;
};

} // namespace sro
