// Writes frames + hits to RNTuples (parameter evio6_file:output_file).
//
// Layout: three flat tables in one file, joined on the frame_number column -
//   frames    : one row per emitted frame
//   fadc_hits : one row per FADC hit (raw address + translated detector address)
//   dcrb_hits : one row per DCRB hit
// RNTuple is used (not TTree) because the end goal is multithreaded writing;
// here a single writer fills sequentially - the Phase-I baseline.

#pragma once

#include <memory>
#include <mutex>
#include <string>

#include <JANA/JEventProcessor.h>

class TFile;
namespace ROOT {
class RNTupleWriter;
}

class SroRNTupleWriter : public JEventProcessor {
public:
    SroRNTupleWriter();

    void Init() override;
    void ProcessSequential(const JEvent& event) override;
    void Finish() override;

private:
    struct Fields; // shared_ptr handles bound to the RNTuple models

    std::string m_output_path = "/data/evio-optim/phase1/sro_hits.root";
    std::unique_ptr<TFile> m_file;
    std::unique_ptr<ROOT::RNTupleWriter> m_frames_writer;
    std::unique_ptr<ROOT::RNTupleWriter> m_fadc_writer;
    std::unique_ptr<ROOT::RNTupleWriter> m_dcrb_writer;
    std::unique_ptr<Fields> m_fields;
    std::mutex m_mutex; // single-threaded in Phase I; lock kept for when nthreads>1
    uint64_t m_frames_written = 0;
    uint64_t m_fadc_written = 0;
    uint64_t m_dcrb_written = 0;
};
