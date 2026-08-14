// sro_recv - streaming receiver: VTP records from TCP into an SRO evio file.
//
// Listens on a TCP port (default 6000). Whatever connects (a real VTP or
// sro_send) and sends evio frames as records gets consumed: each record's
// payload goes through a large in-memory FIFO ring buffer (so a burst of
// several GB can be absorbed while the disk catches up) and a writer thread
// packs the payloads into EVIO4-style blocks in the output file - the same
// on-disk layout as the offline sro_000791 files, so the evio-optim /
// jana4ml4fpga chain can read the result.
//
// Payloads are opaque: nothing inside them is modified or interpreted except
// payload[0] (length cross-check) and payload[5] (frame number, stats only).
//
// Wire framing is auto-detected from the first bytes of the connection:
//   cmsg   - [2 cMsg words][8-word record header][payload]   (coda_sro flavor)
//   record - [8-word record header][payload]
//   raw    - bare evio banks: [w0 = exclusive length][...]
// plus an optional 2-word connection banner (magic, version) before the first
// record. Use --framing to override, --dump-first to see raw words on-site.
//
// Build: strict C++11 (RHEL gcc 4.8 target), no dependencies. See Makefile.

#include "sro_stream_common.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <sys/time.h>

using namespace sro_stream;

static volatile sig_atomic_t g_stop = 0;
static void OnSigInt(int) {
    if (g_stop) _exit(1); // second Ctrl-C: hard exit
    g_stop = 1;
}

static double NowSeconds() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return static_cast<double>(tv.tv_sec) + static_cast<double>(tv.tv_usec) * 1e-6;
}

// ---------------------------------------------------------------------------
// FIFO ring buffer (single producer = socket reader, single consumer = file
// writer). Records are stored as [u32 byte_count][bytes]; each record is
// copied under one mutex hold, so the consumer always sees whole records.
// Push blocks when the ring is full - TCP backpressure then throttles the
// sender, nothing is dropped.
// ---------------------------------------------------------------------------
class RecordRing {
public:
    explicit RecordRing(size_t capacity_bytes)
        : m_buffer(capacity_bytes), m_capacity(capacity_bytes) {}

    /// Blocks until there is room. Returns false if capacity can never fit the record.
    bool Push(const void* data, uint32_t bytes) {
        size_t needed = sizeof(uint32_t) + bytes;
        if (needed > m_capacity) return false;
        std::unique_lock<std::mutex> lock(m_mutex);
        while (m_capacity - m_used < needed && !m_shutdown) {
            m_not_full.wait_for(lock, std::chrono::milliseconds(200));
        }
        if (m_shutdown) return true; // discard silently during shutdown
        CopyIn(&bytes, sizeof(uint32_t));
        CopyIn(data, bytes);
        m_used += needed;
        m_records++;
        m_not_empty.notify_one();
        return true;
    }

    /// Pops one record into `out`. Returns false when the ring is empty and
    /// end-of-stream was signaled.
    bool Pop(std::vector<uint8_t>& out) {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (m_records == 0 && !m_eof) {
            m_not_empty.wait_for(lock, std::chrono::milliseconds(200));
        }
        if (m_records == 0) return false;
        uint32_t bytes = 0;
        CopyOut(&bytes, sizeof(uint32_t));
        out.resize(bytes);
        CopyOut(out.data(), bytes);
        m_used -= sizeof(uint32_t) + bytes;
        m_records--;
        m_not_full.notify_one();
        return true;
    }

    void SetEof() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_eof = true;
        m_not_empty.notify_all();
    }

    /// Unblocks a producer stuck in Push() during emergency shutdown.
    void Shutdown() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_shutdown = true;
        m_eof = true;
        m_not_full.notify_all();
        m_not_empty.notify_all();
    }

    size_t UsedBytes() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_used;
    }

    size_t Capacity() const { return m_capacity; }

private:
    // Wrap-around copies; positions are only touched under m_mutex.
    void CopyIn(const void* data, size_t bytes) {
        const uint8_t* src = static_cast<const uint8_t*>(data);
        size_t first = std::min(bytes, m_capacity - m_write_pos);
        std::memcpy(m_buffer.data() + m_write_pos, src, first);
        std::memcpy(m_buffer.data(), src + first, bytes - first);
        m_write_pos = (m_write_pos + bytes) % m_capacity;
    }
    void CopyOut(void* data, size_t bytes) {
        uint8_t* dst = static_cast<uint8_t*>(data);
        size_t first = std::min(bytes, m_capacity - m_read_pos);
        std::memcpy(dst, m_buffer.data() + m_read_pos, first);
        std::memcpy(dst + first, m_buffer.data(), bytes - first);
        m_read_pos = (m_read_pos + bytes) % m_capacity;
    }

    std::vector<uint8_t> m_buffer;
    size_t m_capacity;
    size_t m_read_pos = 0;
    size_t m_write_pos = 0;
    size_t m_used = 0;
    size_t m_records = 0;
    bool m_eof = false;
    bool m_shutdown = false;
    std::mutex m_mutex;
    std::condition_variable m_not_full;
    std::condition_variable m_not_empty;
};

// ---------------------------------------------------------------------------
// Options / shared state
// ---------------------------------------------------------------------------

struct Options {
    int port;
    std::string output;
    long long buffer_mb;      // ring buffer capacity
    long long block_kb;       // target on-disk block size
    long long idle_timeout_s; // 0 = wait forever
    long long max_events;     // 0 = unlimited
    long long max_mb;         // 0 = unlimited
    long long report_sec;
    long long dump_first;     // hexdump the first words received
    std::string framing;      // auto | cmsg | record | raw
    bool keep_listening;      // accept further connections after one ends
    long long max_record_mb;  // desync guard

    Options()
        : port(6000), output("received.evio"), buffer_mb(1024), block_kb(2048),
          idle_timeout_s(0), max_events(0), max_mb(0), report_sec(2),
          dump_first(100), framing("auto"), keep_listening(false), max_record_mb(64) {}
};

// Captures the first N wire bytes of a connection and prints them once in the
// pyevio hex format - when the capture fills, or on destruction (connection
// ended or protocol error before N words arrived). Every socket read of the
// connection funnels through Append(), so the dump shows the true byte stream
// regardless of framing mode or where parsing failed.
struct WireCapture {
    std::vector<uint8_t> data;
    size_t limit_bytes;
    bool printed;
    std::string peer;

    WireCapture(long long words, const std::string& peer_text)
        : limit_bytes(static_cast<size_t>(words > 0 ? words : 0) * 4), printed(false), peer(peer_text) {
        data.reserve(limit_bytes);
    }

    ~WireCapture() {
        if (!printed && !data.empty()) Print(" (connection ended before the capture filled)");
    }

    void Append(const void* chunk, size_t bytes) {
        if (printed || data.size() >= limit_bytes) return;
        size_t take = std::min(bytes, limit_bytes - data.size());
        const uint8_t* p = static_cast<const uint8_t*>(chunk);
        data.insert(data.end(), p, p + take);
        if (data.size() >= limit_bytes) Print("");
    }

    void Print(const char* suffix) {
        char title[256];
        std::snprintf(title, sizeof(title), "sro_recv: first %zu words on the wire from %s%s",
                      data.size() / 4, peer.c_str(), suffix);
        PrintWireDump(data.data(), data.size(), title);
        printed = true;
    }
};

struct Stats {
    std::atomic<uint64_t> records;
    std::atomic<uint64_t> bytes;          // wire bytes (incl. framing + headers)
    std::atomic<uint64_t> payload_bytes;
    std::atomic<uint64_t> written_blocks;
    std::atomic<uint64_t> written_bytes;
    std::atomic<uint64_t> length_mismatches; // payload[0] != payload_words-1
    std::atomic<uint64_t> out_of_order;      // frame number <= previous
    std::atomic<uint32_t> first_frame;
    std::atomic<uint32_t> last_frame;
    std::atomic<bool> saw_frame;

    Stats()
        : records(0), bytes(0), payload_bytes(0), written_blocks(0), written_bytes(0),
          length_mismatches(0), out_of_order(0), first_frame(0), last_frame(0), saw_frame(false) {}
};

static void PrintUsage(const char* prog) {
    std::printf(
        "sro_recv - receive a VTP evio-frame stream and save it as an SRO evio file\n"
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "Options (--name=value or --name value):\n"
        "  --port N           listen port (default 6000)\n"
        "  --output FILE      output evio file (default received.evio)\n"
        "  --buffer-mb N      FIFO ring buffer size in MB (default 1024; e.g. 8192 for 8 GB)\n"
        "  --block-kb N       target on-disk block size in kB (default 2048, like the DAQ)\n"
        "  --idle-timeout N   close the connection after N seconds without data (default 0 = never)\n"
        "  --max-events N     stop after N records (default 0 = unlimited)\n"
        "  --max-mb N         stop after N MB of payload (default 0 = unlimited)\n"
        "  --framing MODE     auto | cmsg | record | raw (default auto)\n"
        "  --dump-first N     pyevio-style dump of the first N words received (default 100; 0 = off)\n"
        "  --keep-listening   after a connection ends, accept the next one (append to the same file)\n"
        "  --report-sec N     stats line period in seconds (default 2)\n"
        "  --max-record-mb N  reject records larger than N MB as protocol desync (default 64)\n"
        "  --help             this text\n"
        "\n"
        "Example: %s --port=6000 --output=/data/test.evio --buffer-mb=4096 --idle-timeout=30\n",
        prog, prog);
}

// Socket read with 200 ms poll granularity (via SO_RCVTIMEO) so Ctrl-C, idle
// timeout and stop conditions stay responsive. Received bytes are teed into
// `capture` for the first-words debug dump. Returns 1 ok, 0 clean EOF at
// record boundary, -1 error/mid-record EOF, -2 idle timeout, -3 stop requested.
static int RecvExact(int fd, void* data, size_t bytes, long long idle_timeout_s, WireCapture* capture) {
    char* p = static_cast<char*>(data);
    size_t got = 0;
    double idle_deadline = (idle_timeout_s > 0) ? NowSeconds() + idle_timeout_s : 0;
    while (got < bytes) {
        if (g_stop) return -3;
        ssize_t n = ::recv(fd, p + got, bytes - got, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (idle_timeout_s > 0 && NowSeconds() >= idle_deadline) return -2;
                continue;
            }
            return -1;
        }
        if (n == 0) return (got == 0) ? 0 : -1;
        if (capture != NULL) capture->Append(p + got, static_cast<size_t>(n));
        got += static_cast<size_t>(n);
        if (idle_timeout_s > 0) idle_deadline = NowSeconds() + idle_timeout_s;
    }
    return 1;
}

// Reads the remaining payload words of one record. The first `preread_words`
// payload words may already be available in `preread` (framing detection
// consumes a few words ahead).
static bool ReadPayload(int fd, const Options& opt, std::vector<uint32_t>& payload,
                        uint32_t payload_words, const uint32_t* preread, size_t preread_words,
                        WireCapture* capture) {
    payload.resize(payload_words);
    size_t have = std::min<size_t>(preread_words, payload_words);
    if (have > 0) {
        std::memcpy(payload.data(), preread, have * sizeof(uint32_t));
    }
    int rc = RecvExact(fd, payload.data() + have, (payload_words - have) * sizeof(uint32_t), opt.idle_timeout_s, capture);
    if (rc != 1) {
        std::fprintf(stderr, "ERROR: stream ended mid-record (%u payload words expected)\n", payload_words);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Writer thread: pops payloads from the ring, packs them into evio blocks.
// ---------------------------------------------------------------------------
static void WriterThread(RecordRing* ring, const Options* opt, Stats* stats, FILE* file) {
    std::vector<uint8_t> record;
    std::vector<uint32_t> block_body;
    uint32_t block_events = 0;
    uint32_t block_number = 1;
    const size_t target_body_bytes = static_cast<size_t>(opt->block_kb) * 1024;
    block_body.reserve(target_body_bytes / 4 + 65536);

    // Writes the accumulated body as one block: 8-word header + body.
    // (declared as a plain lambda; called from two places below)
    auto flush_block = [&]() {
        if (block_events == 0) return;
        BlockHeader header;
        header.total_words = kBlockHeaderWords + static_cast<uint32_t>(block_body.size());
        header.block_number = block_number;
        header.header_words = kBlockHeaderWords;
        header.event_count = block_events;
        header.reserved1 = 0;
        header.version = kBlockVersion;
        header.reserved2 = 0;
        header.magic = kMagic;
        if (std::fwrite(&header, sizeof(header), 1, file) != 1 ||
            std::fwrite(block_body.data(), sizeof(uint32_t), block_body.size(), file) != block_body.size()) {
            std::fprintf(stderr, "ERROR: writing output file failed (%s) - stopping\n", std::strerror(errno));
            g_stop = 1;
            ring->Shutdown();
            return;
        }
        stats->written_blocks++;
        stats->written_bytes += sizeof(header) + block_body.size() * sizeof(uint32_t);
        block_number++;
        block_events = 0;
        block_body.clear();
    };

    while (ring->Pop(record)) {
        size_t record_words = record.size() / 4;
        if (block_events > 0 && (block_body.size() + record_words) * 4 > target_body_bytes) {
            flush_block();
        }
        const uint32_t* words = reinterpret_cast<const uint32_t*>(record.data());
        block_body.insert(block_body.end(), words, words + record_words);
        block_events++;
    }
    flush_block(); // partial last block
    std::fflush(file);
}

// ---------------------------------------------------------------------------
// One TCP connection: detect framing, then read records into the ring.
// Returns true when the connection ended cleanly (sender closed / idle
// timeout) and the process may accept another; false when the process must
// stop (protocol error, stop conditions, signal).
// ---------------------------------------------------------------------------
static bool ServeConnection(int fd, const Options& opt, Stats* stats, RecordRing* ring, const std::string& peer) {
    // 200 ms receive timeout drives the poll loop in RecvExact
    struct timeval rcv_timeout;
    rcv_timeout.tv_sec = 0;
    rcv_timeout.tv_usec = 200 * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcv_timeout, sizeof(rcv_timeout));

    // First-words debug dump (--dump-first): tees every received byte until N
    // words are captured; prints when full, or on scope exit if the
    // connection/parsing dies earlier. This is the tool for "something started
    // sending and nothing works".
    WireCapture capture(opt.dump_first, peer);

    // --- framing detection on the first 10 words ---
    // cmsg:   magic at word 9   (2 cMsg words + 8-word record header)
    // record: magic at word 7   (8-word record header first)
    // banner: word 0 itself is the magic (2-word connection banner, then retry)
    std::string mode = opt.framing;
    uint32_t peek[10];
    int rc = RecvExact(fd, peek, sizeof(uint32_t), opt.idle_timeout_s, &capture);
    if (rc == 0 || rc == -2) { std::printf("sro_recv: connection ended before any data\n"); return true; }
    if (rc != 1) return false;
    rc = RecvExact(fd, peek + 1, 9 * sizeof(uint32_t), opt.idle_timeout_s, &capture);
    if (rc != 1) { std::fprintf(stderr, "ERROR: connection ended inside the first 10 words\n"); return rc == 0; }

    if (peek[0] == kMagic && peek[1] != kMagic) {
        // 2-word connection banner (magic, version) - the commented-out
        // "emuData" path in coda_sro.c. Shift it away and keep detecting.
        std::printf("sro_recv: connection banner detected (version word 0x%08x)\n", peek[1]);
        std::memmove(peek, peek + 2, 8 * sizeof(uint32_t));
        rc = RecvExact(fd, peek + 8, 2 * sizeof(uint32_t), opt.idle_timeout_s, &capture);
        if (rc != 1) { std::fprintf(stderr, "ERROR: stream ended right after the banner\n"); return rc == 0; }
    }
    if (mode == "auto") {
        if (peek[9] == kMagic) mode = "cmsg";
        else if (peek[7] == kMagic) mode = "record";
        else {
            std::fprintf(stderr, "ERROR: cannot auto-detect framing - no magic 0xc0da0100 at word 7 or 9.\n");
            if (opt.dump_first <= 0) DumpWords(peek, 10, "First words received"); // WireCapture prints the full dump otherwise
            std::fprintf(stderr, "If this is a bare evio-bank stream, rerun with --framing=raw.\n");
            return false;
        }
    }
    if (peek[7] == kMagicByteSwapped || peek[9] == kMagicByteSwapped) {
        std::fprintf(stderr, "ERROR: byte-swapped magic on the wire - big-endian sender is not supported\n");
        return false;
    }
    std::printf("sro_recv: framing mode '%s'\n", mode.c_str());

    std::vector<uint32_t> payload;
    payload.reserve(1 << 16);
    const uint64_t max_record_words = static_cast<uint64_t>(opt.max_record_mb) * 1024 * 1024 / 4;
    const size_t frame_words = (mode == "cmsg") ? 2 : 0;
    uint32_t prev_frame = 0;
    bool warned_buffer_full = false;
    bool first_record = true;

    while (!g_stop) {
        uint32_t payload_words = 0;
        const uint32_t* preread = NULL;
        size_t preread_count = 0;

        if (mode == "raw") {
            uint32_t w0;
            if (first_record) {
                w0 = peek[0];
                preread = &peek[1]; // peek words 1..9 = payload words 1..9
                preread_count = 9;
            } else {
                rc = RecvExact(fd, &w0, sizeof(w0), opt.idle_timeout_s, &capture);
                if (rc == 0) { std::printf("sro_recv: sender closed the connection\n"); return true; }
                if (rc == -2) { std::printf("sro_recv: idle timeout (%lld s) - closing connection\n", opt.idle_timeout_s); return true; }
                if (rc != 1) return false;
            }
            payload_words = w0 + 1; // w0 is the exclusive bank length
            if (payload_words < 2 || payload_words > max_record_words ||
                (first_record && payload_words < 1 + preread_count)) {
                std::fprintf(stderr, "ERROR: implausible raw bank length %u words - protocol desync\n", payload_words);
                return false;
            }
            payload.resize(payload_words);
            payload[0] = w0;
            if (preread_count > 0) std::memcpy(&payload[1], preread, preread_count * sizeof(uint32_t));
            rc = RecvExact(fd, &payload[1 + preread_count], (payload_words - 1 - preread_count) * sizeof(uint32_t), opt.idle_timeout_s, &capture);
            if (rc != 1) { std::fprintf(stderr, "ERROR: stream ended mid-record\n"); return false; }
        } else {
            uint32_t header_words[kRecordHeaderWords];
            if (first_record) {
                std::memcpy(header_words, peek + frame_words, kRecordHeaderWords * sizeof(uint32_t));
                if (frame_words == 0) {
                    preread = &peek[8]; // record mode: 2 payload words came with the peek
                    preread_count = 2;
                }
            } else {
                if (frame_words > 0) {
                    uint32_t framing[2];
                    rc = RecvExact(fd, framing, sizeof(framing), opt.idle_timeout_s, &capture);
                } else {
                    rc = RecvExact(fd, header_words, sizeof(header_words), opt.idle_timeout_s, &capture);
                }
                if (rc == 0) { std::printf("sro_recv: sender closed the connection\n"); return true; }
                if (rc == -2) { std::printf("sro_recv: idle timeout (%lld s) - closing connection\n", opt.idle_timeout_s); return true; }
                if (rc != 1) return false;
                if (frame_words > 0) {
                    rc = RecvExact(fd, header_words, sizeof(header_words), opt.idle_timeout_s, &capture);
                    if (rc != 1) { std::fprintf(stderr, "ERROR: stream ended between framing and header\n"); return false; }
                }
            }

            if (header_words[7] != kMagic) {
                std::fprintf(stderr, "ERROR: record header magic mismatch (got 0x%08x) - protocol desync\n", header_words[7]);
                DumpWords(header_words, kRecordHeaderWords, "Offending header");
                return false;
            }
            if (header_words[0] <= kRecordHeaderWords || header_words[0] > max_record_words) {
                std::fprintf(stderr, "ERROR: implausible record total_length %u words\n", header_words[0]);
                return false;
            }
            payload_words = header_words[0] - kRecordHeaderWords;
            if (!ReadPayload(fd, opt, payload, payload_words, preread, preread_count, &capture)) return false;
        }
        first_record = false;

        // Cross-check payload[0]; count mismatches but keep the data.
        if (payload_words >= 1 && payload[0] != payload_words - 1) {
            if (stats->length_mismatches++ == 0) {
                std::fprintf(stderr, "WARNING: payload[0]=%u does not match payload length %u words (counted, not fatal)\n",
                             payload[0], payload_words);
            }
        }
        uint32_t frame_number = (payload_words > 5) ? payload[5] : 0;
        if (!stats->saw_frame.load()) {
            stats->first_frame = frame_number;
            stats->saw_frame = true;
        } else if (frame_number <= prev_frame) {
            stats->out_of_order++;
        }
        prev_frame = frame_number;
        stats->last_frame = frame_number;

        if (!warned_buffer_full && ring->UsedBytes() > ring->Capacity() / 10 * 9) {
            std::printf("sro_recv: WARNING - ring buffer >90%% full, TCP backpressure engaged (disk slower than input)\n");
            warned_buffer_full = true;
        }
        if (!ring->Push(payload.data(), payload_words * sizeof(uint32_t))) {
            std::fprintf(stderr, "ERROR: record (%u words) larger than the ring buffer\n", payload_words);
            return false;
        }
        stats->records++;
        stats->payload_bytes += static_cast<uint64_t>(payload_words) * 4;
        stats->bytes += static_cast<uint64_t>(payload_words) * 4 +
                        ((mode == "raw") ? 0 : kRecordHeaderWords * 4) + frame_words * 4;

        if (opt.max_events > 0 && stats->records >= static_cast<uint64_t>(opt.max_events)) {
            std::printf("sro_recv: --max-events=%lld reached\n", opt.max_events);
            return false;
        }
        if (opt.max_mb > 0 && stats->payload_bytes >= static_cast<uint64_t>(opt.max_mb) * 1000000ull) {
            std::printf("sro_recv: --max-mb=%lld reached\n", opt.max_mb);
            return false;
        }
    }
    return false; // g_stop
}

int main(int argc, char** argv) {
    Options opt;
    ArgParser args(argc, argv);
    while (args.index < argc) {
        std::string value;
        if (args.MatchFlag("--help") || args.MatchFlag("-h")) { PrintUsage(argv[0]); return 0; }
        else if (args.Match("--port", value)) opt.port = static_cast<int>(ParseLongOrDie(value, "--port"));
        else if (args.Match("--output", value)) opt.output = value;
        else if (args.Match("--buffer-mb", value)) opt.buffer_mb = ParseLongOrDie(value, "--buffer-mb");
        else if (args.Match("--block-kb", value)) opt.block_kb = ParseLongOrDie(value, "--block-kb");
        else if (args.Match("--idle-timeout", value)) opt.idle_timeout_s = ParseLongOrDie(value, "--idle-timeout");
        else if (args.Match("--max-events", value)) opt.max_events = ParseLongOrDie(value, "--max-events");
        else if (args.Match("--max-mb", value)) opt.max_mb = ParseLongOrDie(value, "--max-mb");
        else if (args.Match("--framing", value)) opt.framing = value;
        else if (args.Match("--dump-first", value)) opt.dump_first = ParseLongOrDie(value, "--dump-first");
        else if (args.Match("--report-sec", value)) opt.report_sec = ParseLongOrDie(value, "--report-sec");
        else if (args.Match("--max-record-mb", value)) opt.max_record_mb = ParseLongOrDie(value, "--max-record-mb");
        else if (args.MatchFlag("--keep-listening")) opt.keep_listening = true;
        else {
            std::fprintf(stderr, "ERROR: unknown argument %s (see --help)\n", argv[args.index]);
            return 1;
        }
    }
    if (opt.framing != "auto" && opt.framing != "cmsg" && opt.framing != "record" && opt.framing != "raw") {
        std::fprintf(stderr, "ERROR: --framing must be auto|cmsg|record|raw\n");
        return 1;
    }
    if (opt.report_sec <= 0) opt.report_sec = 2;

    std::signal(SIGINT, OnSigInt);
    std::signal(SIGPIPE, SIG_IGN);

    FILE* out_file = std::fopen(opt.output.c_str(), "wb");
    if (out_file == NULL) {
        std::fprintf(stderr, "ERROR: cannot create %s\n", opt.output.c_str());
        return 1;
    }

    RecordRing ring(static_cast<size_t>(opt.buffer_mb) * 1024 * 1024);
    Stats stats;
    std::thread writer(WriterThread, &ring, &opt, &stats, out_file);

    // Periodic stats printer
    std::atomic<bool> stats_done(false);
    std::thread stats_thread([&]() {
        uint64_t last_records = 0, last_bytes = 0;
        double last_time = NowSeconds();
        while (!stats_done) {
            for (long long i = 0; i < 10 * opt.report_sec && !stats_done; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (stats_done) break;
            double now = NowSeconds();
            uint64_t records = stats.records, bytes = stats.bytes;
            double dt = now - last_time;
            std::printf("sro_recv: %llu frames (%.1f MB wire, %.0f fr/s, %.1f MB/s) | buffer %.1f%% | written %llu blocks %.1f MB | frames %u..%u%s\n",
                        (unsigned long long)records, bytes / 1e6,
                        (dt > 0) ? (records - last_records) / dt : 0.0,
                        (dt > 0) ? (bytes - last_bytes) / dt / 1e6 : 0.0,
                        100.0 * ring.UsedBytes() / ring.Capacity(),
                        (unsigned long long)stats.written_blocks.load(), stats.written_bytes / 1e6,
                        stats.first_frame.load(), stats.last_frame.load(),
                        stats.out_of_order.load() ? " [non-monotonic]" : "");
            std::fflush(stdout);
            last_records = records;
            last_bytes = bytes;
            last_time = now;
        }
    });

    // Listen socket
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    int reuse = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(opt.port));
    if (::bind(listen_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::fprintf(stderr, "ERROR: cannot bind port %d (%s)\n", opt.port, std::strerror(errno));
        return 1;
    }
    if (::listen(listen_fd, 1) < 0) {
        std::fprintf(stderr, "ERROR: listen failed (%s)\n", std::strerror(errno));
        return 1;
    }
    std::printf("sro_recv: listening on port %d, output %s, buffer %lld MB, block target %lld kB\n",
                opt.port, opt.output.c_str(), opt.buffer_mb, opt.block_kb);

    bool accept_more = true;
    while (accept_more && !g_stop) {
        // Timed select-accept so Ctrl-C works while waiting for a connection
        struct timeval tv;
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(listen_fd, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 200 * 1000;
        int ready = ::select(listen_fd + 1, &fds, NULL, NULL, &tv);
        if (ready <= 0) continue;

        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        int conn_fd = ::accept(listen_fd, reinterpret_cast<struct sockaddr*>(&peer), &peer_len);
        if (conn_fd < 0) continue;
        char peer_text[INET_ADDRSTRLEN] = "?";
        ::inet_ntop(AF_INET, &peer.sin_addr, peer_text, sizeof(peer_text));
        std::printf("sro_recv: connection from %s\n", peer_text);

        accept_more = ServeConnection(conn_fd, opt, &stats, &ring, peer_text) && opt.keep_listening;
        ::close(conn_fd);
        if (accept_more && !g_stop) std::printf("sro_recv: waiting for the next connection (--keep-listening)\n");
    }
    ::close(listen_fd);

    // Drain: let the writer finish everything already in the ring
    std::printf("sro_recv: draining buffer to disk (%zu bytes queued) ...\n", ring.UsedBytes());
    ring.SetEof();
    writer.join();
    stats_done = true;
    stats_thread.join();
    std::fclose(out_file);

    std::printf("sro_recv: DONE - %llu frames, %.1f MB payload -> %llu blocks, %.1f MB in %s\n",
                (unsigned long long)stats.records.load(), stats.payload_bytes / 1e6,
                (unsigned long long)stats.written_blocks.load(), stats.written_bytes / 1e6,
                opt.output.c_str());
    std::printf("sro_recv: frames %u..%u, out-of-order records: %llu, payload length mismatches: %llu\n",
                stats.first_frame.load(), stats.last_frame.load(),
                (unsigned long long)stats.out_of_order.load(),
                (unsigned long long)stats.length_mismatches.load());
    return 0;
}
