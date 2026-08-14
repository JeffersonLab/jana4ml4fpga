// sro_send - VTP stream emulator for receiver tests.
//
// Reads SRO evio files (EVIO4-style blocks holding streaming frame sets, e.g.
// /data/sro_boyarinov_data_2026/sro_000791.evio.*), walks the events inside
// each block, and sends every event as one VTP-style record over TCP:
// [2 cMsg words][8-word record header][payload]. Events are sent one by one
// ("frame by frame"), never as whole blocks, and payloads are not modified.
//
// The tool does not interpret the payload beyond payload[0] (length
// cross-check) and payload[5] (frame number for the record header), so it
// works for aggregated frame-set files and would work for raw per-VTP
// capture files alike.
//
// Build: strict C++11 (RHEL gcc 4.8 target), no dependencies. See Makefile.

#include "sro_stream_common.h"

#include <csignal>
#include <string>
#include <vector>

#include <sys/time.h>

using namespace sro_stream;

static volatile sig_atomic_t g_stop = 0;
static void OnSigInt(int) { g_stop = 1; }

static double NowSeconds() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return static_cast<double>(tv.tv_sec) + static_cast<double>(tv.tv_usec) * 1e-6;
}

struct Options {
    std::string host;
    int port;
    long long max_events;    // 0 = all
    long long delay_us;      // sleep between records
    bool loop;               // restart from the first file when input is exhausted
    bool send_cmsg;          // prepend the 2 cMsg framing words
    uint32_t roc_id;
    int connect_timeout_sec; // keep retrying the connect this long
    long long report_every;  // progress line period, in records

    Options()
        : host("localhost"), port(6000), max_events(0), delay_us(0), loop(false),
          send_cmsg(true), roc_id(0), connect_timeout_sec(30), report_every(1000) {}
};

static void PrintUsage(const char* prog) {
    std::printf(
        "sro_send - send SRO evio file events as a VTP-style TCP record stream\n"
        "\n"
        "Usage: %s [options] <file.evio> [more files...]\n"
        "\n"
        "Options (--name=value or --name value):\n"
        "  --host HOST        receiver host (default localhost)\n"
        "  --port N           receiver port (default 6000)\n"
        "  --nevents N        stop after N events; 0 = send everything (default 0)\n"
        "  --delay-us N       sleep N microseconds between records (default 0 = full speed)\n"
        "  --rocid N          roc_id to put in record headers (default 0)\n"
        "  --loop             when input files are exhausted, start over (soak tests)\n"
        "  --no-cmsg          do not send the 2 cMsg framing words before each record\n"
        "  --connect-timeout N  keep retrying the connect for N seconds (default 30)\n"
        "  --report-every N   progress line every N records (default 1000)\n"
        "  --help             this text\n"
        "\n"
        "Example (test loop against sro_recv on the same machine):\n"
        "  %s --port=6000 --nevents=500 /data/sro_boyarinov_data_2026/sro_000791.evio.00000\n",
        prog, prog);
}

/// Sends one record; returns false on socket error.
static bool SendRecord(int fd, const Options& opt, const uint32_t* payload, uint32_t payload_words, uint64_t record_counter) {
    RecordHeader header;
    header.total_length = kRecordHeaderWords + payload_words;
    header.record_counter = static_cast<uint32_t>(record_counter);
    header.header_length = kRecordHeaderWords;
    header.event_count = 1;
    header.roc_id = opt.roc_id;
    header.evio_version = 0x204; // value observed from CODA senders; receivers ignore it
    header.frame_counter = (payload_words > 5) ? payload[5] : 0;
    header.magic = kMagic;

    // cMsg framing: word0 = cMsg header length, word1 = following bytes.
    // coda_sro reads and discards both; word1 is filled truthfully anyway.
    uint32_t cmsg[2];
    cmsg[0] = 2 * sizeof(uint32_t);
    cmsg[1] = header.total_length * sizeof(uint32_t);

    struct iovec iov[3];
    int iov_count = 0;
    if (opt.send_cmsg) {
        iov[iov_count].iov_base = cmsg;
        iov[iov_count].iov_len = sizeof(cmsg);
        iov_count++;
    }
    iov[iov_count].iov_base = &header;
    iov[iov_count].iov_len = sizeof(header);
    iov_count++;
    iov[iov_count].iov_base = const_cast<uint32_t*>(payload);
    iov[iov_count].iov_len = payload_words * sizeof(uint32_t);
    iov_count++;

    // writev with partial-write continuation
    size_t total = 0;
    for (int i = 0; i < iov_count; i++) total += iov[i].iov_len;
    int idx = 0;
    while (total > 0) {
        ssize_t n = ::writev(fd, &iov[idx], iov_count - idx);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        total -= static_cast<size_t>(n);
        size_t skip = static_cast<size_t>(n);
        while (idx < iov_count && skip >= iov[idx].iov_len) {
            skip -= iov[idx].iov_len;
            idx++;
        }
        if (idx < iov_count && skip > 0) {
            iov[idx].iov_base = static_cast<char*>(iov[idx].iov_base) + skip;
            iov[idx].iov_len -= skip;
        }
    }
    return true;
}

int main(int argc, char** argv) {
    Options opt;
    std::vector<std::string> files;

    ArgParser args(argc, argv);
    while (args.index < argc) {
        std::string value;
        if (args.MatchFlag("--help") || args.MatchFlag("-h")) { PrintUsage(argv[0]); return 0; }
        else if (args.Match("--host", value)) opt.host = value;
        else if (args.Match("--port", value)) opt.port = static_cast<int>(ParseLongOrDie(value, "--port"));
        else if (args.Match("--nevents", value)) opt.max_events = ParseLongOrDie(value, "--nevents");
        else if (args.Match("--delay-us", value)) opt.delay_us = ParseLongOrDie(value, "--delay-us");
        else if (args.Match("--rocid", value)) opt.roc_id = static_cast<uint32_t>(ParseLongOrDie(value, "--rocid"));
        else if (args.Match("--connect-timeout", value)) opt.connect_timeout_sec = static_cast<int>(ParseLongOrDie(value, "--connect-timeout"));
        else if (args.Match("--report-every", value)) opt.report_every = ParseLongOrDie(value, "--report-every");
        else if (args.MatchFlag("--loop")) opt.loop = true;
        else if (args.MatchFlag("--no-cmsg")) opt.send_cmsg = false;
        else if (argv[args.index][0] == '-') {
            std::fprintf(stderr, "ERROR: unknown option %s (see --help)\n", argv[args.index]);
            return 1;
        }
        else files.push_back(argv[args.index++]);
    }
    if (files.empty()) {
        PrintUsage(argv[0]);
        return 1;
    }

    std::signal(SIGINT, OnSigInt);
    std::signal(SIGPIPE, SIG_IGN); // broken connection reports through write() errno instead

    // Connect (with retry so the receiver can be started in either order)
    std::printf("sro_send: connecting to %s:%d ...\n", opt.host.c_str(), opt.port);
    int fd = -1;
    double connect_deadline = NowSeconds() + opt.connect_timeout_sec;
    while (fd < 0 && !g_stop) {
        fd = ConnectTo(opt.host, opt.port);
        if (fd < 0) {
            if (NowSeconds() >= connect_deadline) {
                std::fprintf(stderr, "ERROR: could not connect to %s:%d within %d s\n", opt.host.c_str(), opt.port, opt.connect_timeout_sec);
                return 1;
            }
            ::usleep(500 * 1000);
        }
    }
    if (g_stop) return 1;
    int nodelay = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    std::printf("sro_send: connected, cMsg framing %s\n", opt.send_cmsg ? "ON" : "OFF");

    uint64_t events_sent = 0;
    uint64_t bytes_sent = 0;
    uint64_t truncated_blocks = 0;
    double t_start = NowSeconds();
    double t_last_report = t_start;
    uint64_t events_last_report = 0;
    bool socket_ok = true;

    std::vector<uint32_t> body; // current block body, reused across blocks

    do { // outer --loop pass
        for (size_t file_index = 0; file_index < files.size() && socket_ok && !g_stop; file_index++) {
            FILE* file = std::fopen(files[file_index].c_str(), "rb");
            if (file == NULL) {
                std::fprintf(stderr, "ERROR: cannot open %s\n", files[file_index].c_str());
                return 1;
            }
            std::printf("sro_send: reading %s\n", files[file_index].c_str());

            while (socket_ok && !g_stop) {
                uint32_t header[kBlockHeaderWords];
                size_t got = std::fread(header, sizeof(uint32_t), kBlockHeaderWords, file);
                if (got != kBlockHeaderWords) {
                    if (got != 0) truncated_blocks++; // partial header at EOF: writer was cut off
                    break;                            // next file
                }
                if (header[7] == kMagicByteSwapped) {
                    std::fprintf(stderr, "ERROR: %s is byte-swapped (big-endian) - not supported\n", files[file_index].c_str());
                    return 1;
                }
                if (header[7] != kMagic || header[2] != kBlockHeaderWords || header[0] < kBlockHeaderWords) {
                    std::fprintf(stderr, "ERROR: bad block header in %s\n", files[file_index].c_str());
                    return 1;
                }
                size_t body_words = header[0] - kBlockHeaderWords;
                body.resize(body_words);
                if (std::fread(body.data(), sizeof(uint32_t), body_words, file) != body_words) {
                    truncated_blocks++; // tail block cut off mid-write - drop it, like the offline reader does
                    break;
                }

                // Walk the events (frame sets) of this block: event = w0+1 words
                size_t pos = 0;
                uint32_t events_in_block = header[3];
                for (uint32_t event_i = 0; event_i < events_in_block && socket_ok && !g_stop; event_i++) {
                    if (pos >= body_words) {
                        std::fprintf(stderr, "ERROR: block %u claims %u events but body ended at event %u\n", header[1], events_in_block, event_i);
                        return 1;
                    }
                    uint32_t event_words = body[pos] + 1; // w0 is the exclusive length
                    if (pos + event_words > body_words) {
                        std::fprintf(stderr, "ERROR: event %u of block %u overruns the block body\n", event_i, header[1]);
                        return 1;
                    }

                    socket_ok = SendRecord(fd, opt, &body[pos], event_words, events_sent);
                    if (!socket_ok) {
                        std::fprintf(stderr, "ERROR: send failed (%s) - receiver gone?\n", std::strerror(errno));
                        break;
                    }
                    events_sent++;
                    bytes_sent += (opt.send_cmsg ? 2 : 0) * 4 + kRecordHeaderWords * 4 + static_cast<uint64_t>(event_words) * 4;
                    pos += event_words;

                    if (opt.delay_us > 0) ::usleep(static_cast<useconds_t>(opt.delay_us));

                    if (opt.report_every > 0 && events_sent % static_cast<uint64_t>(opt.report_every) == 0) {
                        double now = NowSeconds();
                        double dt = now - t_last_report;
                        double inst_rate = (dt > 0) ? (events_sent - events_last_report) / dt : 0.0;
                        std::printf("sro_send: %llu events, %.1f MB, %.0f ev/s, %.1f MB/s\n",
                                    (unsigned long long)events_sent, bytes_sent / 1e6, inst_rate,
                                    (dt > 0) ? (bytes_sent / 1e6) / (now - t_start) : 0.0);
                        t_last_report = now;
                        events_last_report = events_sent;
                    }
                    if (opt.max_events > 0 && events_sent >= static_cast<uint64_t>(opt.max_events)) {
                        g_stop = 1;
                    }
                }
            }
            std::fclose(file);
        }
    } while (opt.loop && socket_ok && !g_stop);

    ::close(fd);
    double elapsed = NowSeconds() - t_start;
    std::printf("sro_send: DONE - %llu events, %.1f MB in %.1f s (%.1f MB/s)%s\n",
                (unsigned long long)events_sent, bytes_sent / 1e6, elapsed,
                (elapsed > 0) ? bytes_sent / 1e6 / elapsed : 0.0,
                socket_ok ? "" : " [stopped on socket error]");
    if (truncated_blocks > 0) {
        std::printf("sro_send: skipped %llu truncated tail block(s) - normal for DAQ-cut files\n", (unsigned long long)truncated_blocks);
    }
    return socket_ok ? 0 : 2;
}
