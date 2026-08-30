#pragma once

#include "../polyphony.h"

namespace vortex_v2 {
namespace runtime {

template <typename ReadVoltage>
inline float read_broadcast(int lane, int channels, ReadVoltage readVoltage)
{
    if (channels <= 0)
        return 0.f;
    return readVoltage(wintoid::polyphony::broadcast_lane(lane, channels));
}

template <typename ResetLane>
inline void prepare_lanes(int previousChannels,
                          int currentChannels,
                          ResetLane resetLane)
{
    wintoid::polyphony::reset_changed_lanes(
        previousChannels, currentChannels, resetLane);
}

template <typename SetChannels, typename ResetBranch>
inline bool select_output_branch(bool connected,
                                 bool& wasConnected,
                                 int channels,
                                 SetChannels setChannels,
                                 ResetBranch resetBranch)
{
    if (!connected) {
        if (wasConnected)
            resetBranch();
        wasConnected = false;
        return false;
    }

    setChannels(channels);
    wasConnected = true;
    return true;
}

} // namespace runtime
} // namespace vortex_v2
