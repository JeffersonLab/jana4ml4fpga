#include "SroBlockReader.h"

#include <cstring>
#include <stdexcept>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace sro {

namespace {
constexpr uint32_t kEvioMagic = 0xC0DA0100;
constexpr uint32_t kEvioMagicSwapped = 0x0001DAC0;
constexpr size_t kHeaderWords = 8;
} // namespace

MappedFile::MappedFile(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        throw std::runtime_error("MappedFile: cannot open " + path);
    }
    struct stat file_stat;
    if (::fstat(fd, &file_stat) != 0) {
        ::close(fd);
        throw std::runtime_error("MappedFile: cannot stat " + path);
    }
    m_bytes = static_cast<size_t>(file_stat.st_size);
    m_base = ::mmap(nullptr, m_bytes, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd); // the mapping keeps its own reference to the file
    if (m_base == MAP_FAILED) {
        m_base = nullptr;
        throw std::runtime_error("MappedFile: mmap failed for " + path);
    }
    ::madvise(m_base, m_bytes, MADV_SEQUENTIAL);
}

MappedFile::~MappedFile() {
    if (m_base != nullptr) {
        ::munmap(m_base, m_bytes);
    }
}

SroBlockReader::SroBlockReader(std::vector<std::string> file_paths, bool use_mmap)
    : m_file_paths(std::move(file_paths)), m_use_mmap(use_mmap) {
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
    m_mapped.reset(); // consumers holding the shared_ptr keep the old mapping alive
    if (m_next_file_index >= m_file_paths.size()) {
        return false;
    }
    m_current_file = m_file_paths[m_next_file_index++];
    if (m_use_mmap) {
        m_mapped = std::make_shared<MappedFile>(m_current_file);
        m_map_pos = 0;
    } else {
        m_file = std::fopen(m_current_file.c_str(), "rb");
        if (m_file == nullptr) {
            throw std::runtime_error("SroBlockReader: cannot open " + m_current_file);
        }
    }
    return true;
}

bool SroBlockReader::ValidateHeader(const uint32_t* header) const {
    uint32_t magic = header[7];
    if (magic == kEvioMagicSwapped) {
        throw std::runtime_error("SroBlockReader: byte-swapped file " + m_current_file + " - big-endian input is not supported by this naive reader");
    }
    if (magic != kEvioMagic || header[2] != kHeaderWords || header[0] < kHeaderWords) {
        throw std::runtime_error("SroBlockReader: bad block header in " + m_current_file);
    }
    return true;
}

bool SroBlockReader::ReadNextBlock(RawBlock& block) {
    return m_use_mmap ? ReadNextBlockMmap(block) : ReadNextBlockFread(block);
}

bool SroBlockReader::ReadNextBlockFread(RawBlock& block) {
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
    ValidateHeader(header);

    block.block_number = header[1];
    block.event_count = header[3];
    block.source_file = m_current_file;
    block.mapping.reset();
    block.words.resize(header[0] - kHeaderWords);
    size_t body_words = block.words.size();
    if (std::fread(block.words.data(), sizeof(uint32_t), body_words, m_file) != body_words) {
        // Short body = the file's tail block was cut off mid-write. Drop it and
        // continue with the next file (recursion depth is bounded by file count).
        m_truncated_tail_blocks++;
        if (!OpenNextFile()) {
            return false;
        }
        return ReadNextBlockFread(block);
    }
    block.body = block.words.data();
    block.body_word_count = body_words;
    return true;
}

bool SroBlockReader::ReadNextBlockMmap(RawBlock& block) {
    if (m_mapped == nullptr && !OpenNextFile()) {
        return false;
    }

    while (true) {
        size_t words_left = m_mapped->WordCount() - m_map_pos;
        if (words_left < kHeaderWords) {
            if (words_left != 0) {
                m_truncated_tail_blocks++; // partial header at EOF
            }
            if (!OpenNextFile()) {
                return false;
            }
            continue;
        }
        const uint32_t* header = m_mapped->Words() + m_map_pos;
        ValidateHeader(header);
        size_t total_words = header[0];
        if (words_left < total_words) {
            m_truncated_tail_blocks++; // body cut off mid-write at EOF
            if (!OpenNextFile()) {
                return false;
            }
            continue;
        }
        block.block_number = header[1];
        block.event_count = header[3];
        block.source_file = m_current_file;
        block.words.clear();
        block.body = header + kHeaderWords;
        block.body_word_count = total_words - kHeaderWords;
        block.mapping = m_mapped;
        m_map_pos += total_words;
        return true;
    }
}

} // namespace sro
