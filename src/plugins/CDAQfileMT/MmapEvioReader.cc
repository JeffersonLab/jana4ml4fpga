#include "MmapEvioReader.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>

MmapEvioReader::MmapEvioReader(const std::string& path) : m_path(path) {
    m_fd = ::open(path.c_str(), O_RDONLY);
    if (m_fd < 0) throw std::runtime_error("MmapEvioReader: cannot open " + path);

    struct stat st{};
    if (::fstat(m_fd, &st) != 0 || st.st_size <= 0) {
        ::close(m_fd);
        throw std::runtime_error("MmapEvioReader: cannot stat " + path);
    }
    void* base = ::mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, m_fd, 0);
    if (base == MAP_FAILED) {
        ::close(m_fd);
        throw std::runtime_error("MmapEvioReader: mmap failed for " + path);
    }
    // Sequential access: enable aggressive kernel readahead
    ::madvise(base, static_cast<size_t>(st.st_size), MADV_SEQUENTIAL);

    m_base = static_cast<const uint32_t*>(base);
    m_size_words = static_cast<uint64_t>(st.st_size) / 4;
}

MmapEvioReader::~MmapEvioReader() {
    if (m_base) ::munmap(const_cast<uint32_t*>(m_base), m_size_words * 4);
    if (m_fd >= 0) ::close(m_fd);
}

MmapEvioReader::Result MmapEvioReader::AdvanceToBlockPayload() {
    while (m_events_left == 0) {
        // Need the next block header. HDEVIO EOF quirk kept: exactly 8 words left
        // (the file trailer block) = end of file.
        uint64_t words_left = m_size_words - m_block_pos;
        if (words_left <= 8) return Result::kEndOfFile;
        if (words_left < 8) return Result::kError;  // truncated header

        // Magic word check + endianness (word 7 of the block header)
        uint32_t magic = m_base[m_block_pos + 7];
        if (magic == 0xc0da0100) {
            m_swap_needed = false;
        } else if (magic == 0x0001dac0) {
            m_swap_needed = true;
        } else {
            return Result::kError;  // bad block header
        }

        uint64_t block_len = word(m_block_pos, m_swap_needed);      // words, inclusive
        uint32_t header_len = word(m_block_pos + 2, m_swap_needed); // normally 8
        uint32_t event_count = word(m_block_pos + 3, m_swap_needed);

        if (block_len < header_len || m_block_pos + block_len > m_size_words) {
            return Result::kError;  // block extends past end of file
        }

        m_event_pos = m_block_pos + header_len;
        m_events_left = event_count;
        m_block_pos += block_len;  // position of the block after this one
        // event_count == 0 (e.g. trailer blocks): loop to the next block
    }

    if (m_prefetch_mb > 0) {
        uint64_t start = m_event_pos * 4;
        uint64_t len = m_prefetch_mb * 1024ull * 1024ull;
        if (start < m_size_words * 4) {
            len = std::min(len, m_size_words * 4 - start);
            ::madvise(const_cast<uint32_t*>(m_base) + m_event_pos, static_cast<size_t>(len),
                      MADV_WILLNEED);
        }
    }
    return Result::kOk;
}

MmapEvioReader::Result MmapEvioReader::NextEvent(EVIOBlockedEvent& block) {
    auto r = AdvanceToBlockPayload();
    if (r != Result::kOk) return r;

    // Event bank: word[0] = exclusive length
    uint64_t event_len = static_cast<uint64_t>(word(m_event_pos, m_swap_needed)) + 1;
    if (m_event_pos + event_len > m_size_words) return Result::kError;

    block.block_number = m_block_number++;
    block.swap_needed = m_swap_needed;
    block.data.assign(m_base + m_event_pos, m_base + m_event_pos + event_len);

    m_event_pos += event_len;
    m_events_left--;
    return Result::kOk;
}

MmapEvioReader::Result MmapEvioReader::NextSpan(EVIOBlockedEvent& block, uint32_t max_events,
                                                uint64_t& events_out) {
    auto r = AdvanceToBlockPayload();
    if (r != Result::kOk) return r;

    uint64_t span_start = m_event_pos;
    uint64_t span_end;

    if (max_events == 0 || max_events >= m_events_left) {
        // Whole remainder of the current physical block: one memcpy, no event walk.
        // Payload ends where the next block begins (m_block_pos was already advanced).
        events_out = m_events_left;
        span_end = m_block_pos;
        m_events_left = 0;
    } else {
        // Sub-block batching: walk event length words to cut the span
        events_out = 0;
        span_end = span_start;
        while (events_out < max_events) {
            uint64_t event_len = static_cast<uint64_t>(word(span_end, m_swap_needed)) + 1;
            if (span_end + event_len > m_size_words) return Result::kError;
            span_end += event_len;
            events_out++;
        }
        m_event_pos = span_end;
        m_events_left -= static_cast<uint32_t>(events_out);
    }

    block.block_number = m_block_number++;
    block.swap_needed = m_swap_needed;
    block.data.assign(m_base + span_start, m_base + span_end);
    return Result::kOk;
}
