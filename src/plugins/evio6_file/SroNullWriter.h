// Dummy writer for performance isolation (parameter evio6_file:writer=null).
//
// Walks exactly the same hit data as SroRNTupleWriter - including the lazy
// EnsureDecoded step, in parallel - and folds it into a checksum (so the
// compiler cannot drop the reads and the memory traffic stays comparable), but
// produces no output file. The wall-clock difference between a 'null' and an
// 'rntuple' run is the cost of RNTuple writing.

#pragma once

#include <atomic>
#include <cstdint>

#include <JANA/JEventProcessor.h>

class SroNullWriter : public JEventProcessor {
public:
    SroNullWriter();

    void ProcessParallel(const JEvent& event) override;
    void Finish() override;

private:
    std::atomic<uint64_t> m_checksum{0};
    std::atomic<uint64_t> m_frames_seen{0};
    std::atomic<uint64_t> m_fadc_seen{0};
    std::atomic<uint64_t> m_dcrb_seen{0};
};
