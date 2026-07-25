#include "SroBlockReader.h"

#include <cstring>
#include <stdexcept>

namespace sro {

namespace {
constexpr uint32_t kEvioMagic = 0xC0DA0100;
constexpr uint32_t kEvioMagicSwapped = 0x0001DAC0;
constexpr size_t kHeaderWords = 8;
} // namespace

SroBlockReader::SroBlockReader(std::vector<std::string> file_paths) : m_file_paths(std::move(file_paths)) {
    if (m_file_paths.empty()) {
        throw std::runtime_error("SroBlockReader: no input files given");
    }
}

SroBlockReader::~SroBlockReader() {
    if (m_file != nullptr) {
        std::fclose(m_file);
    }
}

bool SroBlockReader::OpenNextFile() {
    if (m_file != nullptr) {
        std::fclose(m_file);
        m_file = nullptr;
    }
    if (m_next_file_index >= m_file_paths.size()) {
        return false;
    }
    m_current_file = m_file_paths[m_next_file_index++];
    m_file = std::fopen(m_current_file.c_str(), "rb");
    if (m_file == nullptr) {
        throw std::runtime_error("SroBlockReader: cannot open " + m_current_file);
    }
    return true;
}

bool SroBlockReader::ReadNextBlock(RawBlock& block) {
    if (m_file == nullptr && !OpenNextFile()) {
        return false;
    }

    uint32_t header[kHeaderWords];
    while (true) {
        size_t read_words = std::fread(header, sizeof(uint32_t), kHeaderWords, m_file);
        if (read_words == kHeaderWords) {
            break;
        }
        if (read_words != 0) {
            m_truncated_tail_blocks++; // partial header at EOF - writer was cut off
        }
        if (!OpenNextFile()) {
            return false; // end of the last file
        }
    }

    uint32_t total_length = header[0];
    uint32_t magic = header[7];
    if (magic == kEvioMagicSwapped) {
        throw std::runtime_error("SroBlockReader: byte-swapped file " + m_current_file + " - big-endian input is not supported by this naive reader");
    }
    if (magic != kEvioMagic || header[2] != kHeaderWords || total_length < kHeaderWords) {
        throw std::runtime_error("SroBlockReader: bad block header in " + m_current_file);
    }

    block.block_number = header[1];
    block.event_count = header[3];
    block.source_file = m_current_file;
    block.words.resize(total_length - kHeaderWords);
    size_t body_words = block.words.size();
    if (std::fread(block.words.data(), sizeof(uint32_t), body_words, m_file) != body_words) {
        // Short body = the file's tail block was cut off mid-write. Drop it and
        // continue with the next file (recursion depth is bounded by file count).
        m_truncated_tail_blocks++;
        if (!OpenNextFile()) {
            return false;
        }
        return ReadNextBlock(block);
    }
    return true;
}

} // namespace sro
