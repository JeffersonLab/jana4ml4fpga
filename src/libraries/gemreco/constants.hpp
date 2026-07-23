// gemreco: standalone MT-safe GEM reconstruction library.
// HARD RULE: no JANA, no ROOT dependencies anywhere in gemreco/.
#pragma once

namespace gemreco {

inline constexpr int kChannelsCount = 128;

/// APV25 channel de-multiplexing: maps the readout order channel to the physical
/// strip index. Faithful port of ml4fpga::gem::Constants::ApvChannelCorrection.
constexpr int apv_channel_correction(int channel) {
    return (32 * (channel % 4)) + (8 * (channel / 4)) - (31 * (channel / 16));
}

/// Strip index -> position [mm] on a plane centered at 0.
/// Faithful port of ml4fpga::gem::Constants::CalculateStripPosition.
constexpr double strip_position(int strip_id, double plane_size, int num_apv_on_plane) {
    const double pitch = plane_size / (kChannelsCount * num_apv_on_plane);
    return -0.5 * (plane_size - pitch) + (pitch * strip_id);
}

}  // namespace gemreco
