// gemreco: plain input event data, decoupled from EVIO/JANA types.
#pragma once

#include <cstdint>
#include <vector>

namespace gemreco {

/// One SRS/APV25 readout channel: the time-sample window of one APV channel.
/// Mirrors the fields of DGEMSRSWindowRawData that reconstruction consumes.
struct SrsChannelData {
    int apv_id = -1;
    int raw_channel_apv = -1;           ///< channel as read out (before de-multiplexing)
    std::vector<uint16_t> samples;      ///< ADC time samples
};

struct RawEvent {
    uint64_t event_number = 0;          ///< source event number (for output labeling)
    std::vector<SrsChannelData> srs;
};

}  // namespace gemreco
