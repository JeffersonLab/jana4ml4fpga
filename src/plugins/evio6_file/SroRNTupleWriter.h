// Writes frames + hits to RNTuples (parameter evio6_file:output_file).
//
// Layout: three flat tables in one file, joined on the frame_number column -
//   frames    : one row per emitted frame
//   fadc_hits : one row per FADC hit (raw address + translated detector address)
//   dcrb_hits : one row per DCRB hit
// Filling is parallel (ProcessParallel + one RNTupleFillContext per thread and
// table); entry order on disk is cluster-interleaved, which the order-insensitive
// output contract allows. The three parallel writers share one TFile, which is
// not thread-safe - every cluster commit goes through m_file_mutex, with page
// compression done outside the lock (FlushColumns before FlushCluster).

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <JANA/JEventProcessor.h>

#include <libraries/evio_sro_parser/SroData.h>

class TFile;
namespace ROOT {
class RNTupleParallelWriter;
}

class SroRNTupleWriter : public JEventProcessor {
public:
    SroRNTupleWriter();

    void Init() override;
    void ProcessParallel(const JEvent& event) override;
    void Finish() override;

private:
    struct ThreadContexts; // per-thread fill contexts + bound entry pointers

    ThreadContexts& GetThreadContexts();

    std::string m_output_path = "/data/evio-optim/phase1/sro_hits.root";
    int32_t m_compression = -1; // evio6_file:compression - ROOT setting; -1 keeps the ROOT default
    std::unique_ptr<TFile> m_file;
    std::unique_ptr<ROOT::RNTupleParallelWriter> m_frames_writer;
    std::unique_ptr<ROOT::RNTupleParallelWriter> m_fadc_writer;
    std::unique_ptr<ROOT::RNTupleParallelWriter> m_dcrb_writer;
    std::mutex m_file_mutex;     // serializes all TFile interaction (commits, context creation)
    std::mutex m_registry_mutex; // protects m_thread_contexts
    std::mutex m_decode_stats_mutex;
    sro::ParseStats m_decode_stats; // anomalies from EnsureDecoded (lazy mode)
    std::vector<std::unique_ptr<ThreadContexts>> m_thread_contexts;
    std::atomic<uint64_t> m_frames_written{0};
    std::atomic<uint64_t> m_fadc_written{0};
    std::atomic<uint64_t> m_dcrb_written{0};
};
