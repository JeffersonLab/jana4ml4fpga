# sro_stream_tools — VTP streaming test tools

Two standalone executables for testing a streaming-readout chain where a VTP
streams evio frames over TCP (beam-test setup: one FADC250 + VTP, 9 calorimeter
channels, frames on port 6000):

- **`sro_send`** — VTP emulator. Reads SRO evio files (for example
  `sro_000791.evio.*`), extracts the events (frames) from each block, and sends
  them one by one as VTP-style TCP records. Payload bytes are sent unmodified.
- **`sro_recv`** — receiver. Listens on the port, consumes records from
  whatever connects (real VTP or `sro_send`), buffers payloads in a large
  in-memory FIFO (GB-scale, so a burst can be absorbed while the disk catches
  up), and packs them into an evio file with blocks that the
  evio-optim / `evio6_file` chain reads directly.

Both tools treat the record payload as an opaque blob. Only `payload[0]`
(length cross-check) and `payload[5]` (frame number, statistics only) are
inspected, so aggregated offline files and raw per-VTP frames flow through
identically.

## Build

Deployment target includes RHEL with gcc 4.8, so the code is strict C++11 with
no dependencies beyond pthread and POSIX sockets.

Standalone (copy this directory to the DAQ machine):

```
make
```

Through the project CMake tree: targets `sro_send` and `sro_recv`.

## Wire format

One record, little-endian u32 words, as consumed by the working `coda_sro`
input thread (`dac/main/coda_sro.c`):

```
[ 2 cMsg framing words ]   optional; values ignored by all known receivers
[ 8-word record header ]   {total_len, record#, 8, 1, rocid, version, frame#, 0xc0da0100}
[ payload words ]          payload[0] = exclusive length, payload[5] = frame number
```

`sro_recv` auto-detects the framing from the first 10 words of a connection:
magic at word 9 = `cmsg`, at word 7 = `record` (no cMsg words). A leading
2-word connection banner (magic, version) is skipped if present. A stream of
bare evio banks with no headers needs an explicit `--framing=raw`.

## First-words debug dump

When something starts sending and nothing works, the first bytes on the wire
are the evidence. `sro_recv` captures the first N 32-bit words of every
connection (`--dump-first N`, default 100, `0` disables) and prints them in
the pyevio `hex` format — one word per line with byte, half-word, hex, decimal
and bit views:

```
Idx  Offset    Word#  Bytes        Half-words   Word(hex)  Word(dec)      Bits (MSB->LSB)
-----------------------------------------------------------------------------------------
0    0x000000  0      08 00 00 00      8     0  0x00000008          8  00000000 ... 00001000
...
9    0x000024  9      00 01 da c0    256 49370  0xc0da0100 3235512576  11000000 ... 00000000
```

The capture tees off the receive path, so the dump appears no matter which
framing mode is active and also when parsing fails: if the connection dies or
desyncs before N words arrive, whatever was captured is printed on the spot
(marked "connection ended before the capture filled"). Words are decoded
little-endian, the SRO wire order; `Bytes` shows the raw wire bytes.

## Typical usage

Receiver on the DAQ/storage machine (4 GB buffer, give up after 30 s of
silence):

```
./sro_recv --port=6000 --output=/data/test.evio --buffer-mb=4096 --idle-timeout=30
```

Emulated VTP from a saved file (all events, full speed):

```
./sro_send --host=daqhost --port=6000 /data/sro_boyarinov_data_2026/sro_000791.evio.00000
```

Useful knobs: `sro_send --nevents N --delay-us N --loop --no-cmsg`,
`sro_recv --max-events N --max-mb N --block-kb N --keep-listening --framing MODE`.
`--help` on either tool lists everything.

The receiver prints a stats line every `--report-sec` seconds: frames and MB
received, rates, ring-buffer fill, blocks written, frame-number range. When
the ring passes 90% fill it reports that TCP backpressure is throttling the
sender; nothing is ever dropped.

## Output file format

EVIO4-style blocks, identical layout to the offline `sro_000791` files:
8-word block header `{total_words, block#, 8, event_count, 0, 4, 0,
0xc0da0100}` followed by the concatenated record payloads. Default target
block size 2 MB (`--block-kb`). The `evio6_file` plugin reads the file as-is
when the payloads are aggregated frame sets; raw single-VTP payloads need the
planned parser extension in evio-optim.

## Verification status (2026-08-13, dev machine)

- 550 events of `sro_000791.evio.00000` sent through
  send → TCP → recv → file, then processed with the `evio6_file` chain
  (bypass finder = every hit compared): output IDENTICAL to reading the
  original file directly — same frame set, 4,406,263 FADC hits and
  13,951,524 DCRB hits with equal order-insensitive hashes
  (`space/scripts/verify_output.py` in evio-optim).
- Backpressure: 16 MB buffer at full sender speed engages TCP backpressure;
  output stays byte-identical.
- All three framings (`cmsg`, `record`, `raw`) produce byte-identical files.
- Soak: 5 GB at ~1.4 GB/s over loopback through a 6 GB buffer, ~2 GB backlog
  absorbed and drained, 0 structure errors when parsed back.
