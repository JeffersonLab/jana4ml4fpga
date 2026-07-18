// Dumps golden fixtures from the LEGACY gemrecon chain to CSV files, for
// bit-for-bit parity tests of the new gemreco library (MT plan, phase W2-A).
//
// Temporary tooling: retire together with the legacy factories at phase W2-F.
//
// Run single-threaded alongside gemrecon, e.g.:
//   jana4ml4fpga -Pplugins=CDAQfile,gemrecon,root_output,dqm,gemreco_fixdump \
//     -Pnthreads=1 -Pjana:nevents=600 -Pfixdump:dir=ai_rework/fixtures ... file.evio
#pragma once

#include <fstream>
#include <set>
#include <string>

#include <JANA/JEventProcessor.h>
#include <extensions/spdlog/SpdlogMixin.h>

class FixtureDumpProcessor : public JEventProcessor,
                             public spdlog::extensions::SpdlogMixin<FixtureDumpProcessor> {
public:
    explicit FixtureDumpProcessor(JApplication* app) : JEventProcessor(app) {}

    void Init() override;
    void Process(const std::shared_ptr<const JEvent>& event) override;
    void Finish() override;

private:
    void DumpMapping();
    bool IsCheckpoint(size_t evt_seq) const;

    std::string m_dir = "ai_rework/fixtures";
    size_t m_nevents = 600;                     ///< dump raw/peaks/clusters for first N events
    std::set<size_t> m_checkpoints = {5, 17, 50, 100, 300, 599};  ///< heavy per-stage dumps

    size_t m_evt_seq = 0;   ///< sequential event index (replay order) - single-thread only

    std::ofstream m_srs_raw, m_raw_data, m_pedestal, m_apv_decoded,
                  m_plane_decoded, m_plane_noise, m_peaks, m_clusters, m_samples;
};
