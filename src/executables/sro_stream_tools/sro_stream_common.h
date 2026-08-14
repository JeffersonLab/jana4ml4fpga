// Shared pieces of sro_send / sro_recv: the VTP record wire format, socket
// helpers with EINTR handling, and small argument-parsing utilities.
//
// Wire format (one record), little-endian u32 words, from the working
// coda_sro input thread (dac/main/coda_sro.c:505-570):
//
//   [ 2 cMsg framing words ]   optional on the wire; values are ignored by
//                              every known receiver, only their presence matters
//   [ 8-word record header ]   RecordHeader below, ends with magic 0xc0da0100
//   [ payload words ]          payload_words = total_length - 8;
//                              payload[0] = exclusive payload length (payload_words-1)
//                              payload[5] = frame number (both for raw VTP
//                              time-slice banks and for aggregated frame sets)
//
// The payload is treated as an opaque blob everywhere in these tools: only
// payload[0] (cross-check) and payload[5] (stats) are looked at, so the tools
// work the same for aggregated offline files and for raw single-VTP streams.
//
// Constraint: must compile on RHEL gcc 4.8 => strict C++11, POSIX sockets,
// no std::regex / std::put_time / C++14 library calls.

#ifndef SRO_STREAM_COMMON_H
#define SRO_STREAM_COMMON_H

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

namespace sro_stream {

const uint32_t kMagic = 0xC0DA0100u;
const uint32_t kMagicByteSwapped = 0x0001DAC0u;
const uint32_t kRecordHeaderWords = 8;

// Matches EVIO_Record_Header_t in dac/dac.s/sroLib.h (packed, 8 u32 words).
struct RecordHeader {
    uint32_t total_length;   // inclusive record length in words (header + payload)
    uint32_t record_counter; // incremented per record sent; does not show lost frames
    uint32_t header_length;  // always 8
    uint32_t event_count;    // 1 for a streaming record
    uint32_t roc_id;
    uint32_t evio_version;   // 0x204 observed from CODA senders; receivers ignore it
    uint32_t frame_counter;  // frame number; the VTP leaves 0, coda_sro refills from payload[5]
    uint32_t magic;          // kMagic
};

// EVIO block header as written on disk by the DAQ and expected by
// evio-optim's SroBlockReader: {len_incl, block#, 8, event_count, 0,
// version, 0, magic}. version=4 matches the observed sro_000791 files
// (the value is historical; readers do not check it).
struct BlockHeader {
    uint32_t total_words; // inclusive: 8 + body words
    uint32_t block_number;
    uint32_t header_words; // always 8
    uint32_t event_count;
    uint32_t reserved1;
    uint32_t version;
    uint32_t reserved2;
    uint32_t magic;
};
const uint32_t kBlockHeaderWords = 8;
const uint32_t kBlockVersion = 4;

// ---------------------------------------------------------------------------
// Socket helpers
// ---------------------------------------------------------------------------

/// Writes exactly `bytes` bytes; returns false on error (errno kept).
inline bool WriteAll(int fd, const void* data, size_t bytes) {
    const char* p = static_cast<const char*>(data);
    while (bytes > 0) {
        ssize_t n = ::write(fd, p, bytes);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += n;
        bytes -= static_cast<size_t>(n);
    }
    return true;
}

/// Reads exactly `bytes` bytes. Returns 1 on success, 0 on clean EOF at a
/// record boundary (nothing read yet), -1 on error or mid-record EOF.
inline int ReadAll(int fd, void* data, size_t bytes) {
    char* p = static_cast<char*>(data);
    size_t got = 0;
    while (got < bytes) {
        ssize_t n = ::read(fd, p + got, bytes - got);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) {
            return (got == 0) ? 0 : -1; // EOF; mid-record EOF is an error
        }
        got += static_cast<size_t>(n);
    }
    return 1;
}

/// Resolves host and connects. Returns fd or -1 (message printed).
inline int ConnectTo(const std::string& host, int port) {
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[16];
    std::snprintf(port_str, sizeof(port_str), "%d", port);
    struct addrinfo* result = NULL;
    int rc = ::getaddrinfo(host.c_str(), port_str, &hints, &result);
    if (rc != 0) {
        std::fprintf(stderr, "ERROR: cannot resolve %s: %s\n", host.c_str(), gai_strerror(rc));
        return -1;
    }
    int fd = -1;
    for (struct addrinfo* ai = result; ai != NULL; ai = ai->ai_next) {
        fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(result);
    return fd;
}

// ---------------------------------------------------------------------------
// Argument parsing: accepts --name=value and --name value forms.
// ---------------------------------------------------------------------------

struct ArgParser {
    int argc;
    char** argv;
    int index; // current position, starts after argv[0]

    ArgParser(int argc_, char** argv_) : argc(argc_), argv(argv_), index(1) {}

    /// If argv[index] matches --name, consumes it (and its value) and fills
    /// `value`. Returns true on match.
    bool Match(const char* name, std::string& value) {
        const char* arg = argv[index];
        size_t name_len = std::strlen(name);
        if (std::strncmp(arg, name, name_len) != 0) return false;
        if (arg[name_len] == '=') {
            value = arg + name_len + 1;
            index++;
            return true;
        }
        if (arg[name_len] == '\0') {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "ERROR: %s requires a value\n", name);
                std::exit(1);
            }
            value = argv[index + 1];
            index += 2;
            return true;
        }
        return false;
    }

    bool MatchFlag(const char* name) {
        if (std::strcmp(argv[index], name) == 0) {
            index++;
            return true;
        }
        return false;
    }
};

inline long long ParseLongOrDie(const std::string& text, const char* what) {
    char* end = NULL;
    long long value = std::strtoll(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0') {
        std::fprintf(stderr, "ERROR: bad %s value '%s'\n", what, text.c_str());
        std::exit(1);
    }
    return value;
}

/// Hexdump of the first `words` u32 words, 4 per line, for on-site protocol
/// diagnosis when an unknown sender connects.
inline void DumpWords(const uint32_t* data, size_t words, const char* title) {
    std::printf("%s (%zu words):\n", title, words);
    for (size_t i = 0; i < words; i++) {
        if (i % 4 == 0) std::printf("  [%3zu]", i);
        std::printf(" 0x%08x", data[i]);
        if (i % 4 == 3 || i + 1 == words) std::printf("\n");
    }
}

// ---------------------------------------------------------------------------
// pyevio-style word dump ("pyevio hex" format): one 32-bit word per line with
// byte, half-word, word and bit views. This is the first thing to look at when
// an unknown sender connects and nothing parses.
//
// Idx  Offset    Word#  Bytes        Half-words   Word(hex)  Word(dec)      Bits (MSB->LSB)
// ---------------------------------------------------------------------------

/// Formats a 32-bit word as "00000000 00000000 00000000 00000000" into out[36].
inline void FormatBits(uint32_t word, char* out) {
    int pos = 0;
    for (int bit = 31; bit >= 0; bit--) {
        out[pos++] = (word >> bit) & 1 ? '1' : '0';
        if (bit % 8 == 0 && bit != 0) out[pos++] = ' ';
    }
    out[pos] = '\0';
}

/// Dumps `bytes` bytes (as little-endian u32 words, the SRO wire order) in the
/// pyevio hex format. A trailing partial word is printed as raw bytes.
inline void PrintWireDump(const uint8_t* data, size_t bytes, const char* title) {
    static const char* header =
        "Idx  Offset    Word#  Bytes        Half-words   Word(hex)  Word(dec)      Bits (MSB->LSB)";
    size_t rule_len = std::strlen(header);
    for (size_t i = 0; i < rule_len; i++) std::putchar('=');
    std::printf("\n%s\n", title);
    for (size_t i = 0; i < rule_len; i++) std::putchar('=');
    std::printf("\n%s\n", header);
    for (size_t i = 0; i < rule_len; i++) std::putchar('-');
    std::printf("\n");

    size_t words = bytes / 4;
    for (size_t i = 0; i < words; i++) {
        const uint8_t* b = data + i * 4;
        uint32_t word = static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
                        (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
        uint16_t half_low = static_cast<uint16_t>(word & 0xFFFF);
        uint16_t half_high = static_cast<uint16_t>(word >> 16);
        char bits[36];
        FormatBits(word, bits);
        std::printf("%-4zu 0x%06zx  %-6zu %02x %02x %02x %02x  %5u %5u  0x%08x %10u  %s\n",
                    i, i * 4, i, b[0], b[1], b[2], b[3], half_low, half_high, word, word, bits);
    }
    if (bytes % 4 != 0) {
        std::printf("%-4zu 0x%06zx  %-6zu", words, words * 4, words);
        for (size_t i = words * 4; i < bytes; i++) std::printf(" %02x", data[i]);
        std::printf("   (partial word)\n");
    }
    std::fflush(stdout);
}

} // namespace sro_stream

#endif // SRO_STREAM_COMMON_H
