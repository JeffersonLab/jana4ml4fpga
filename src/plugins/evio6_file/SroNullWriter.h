// Dummy writer for performance isolation (parameter evio6_file:writer=null).
//
// Walks exactly the same hit data as SroRNTupleWriter and folds it into a
// checksum (so the compiler cannot drop the reads and the memory traffic stays
// comparable), but produces no output file. The wall-clock difference between a
// 'null' and an 'rntuple' run is the cost of RNTuple writing.

#pragma once

#include <cstdint>
#include <mutex>

#include <JANA/JEventProcessor.h>

class SroNullWriter : public JEventProcessor {
public:
    SroNullWriter();

    void ProcessSequential(const JEvent& event) override;
    void Finish() override;

private:
    std::mutex m_mutex;
    uint64_t m_checksum = 0;
    uint64_t m_frames_seen = 0;
    uint64_t m_fadc_seen = 0;
    uint64_t m_dcrb_seen = 0;
};
