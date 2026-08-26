#pragma once

namespace wintoid {
namespace polyphony {

static const int MAX_CHANNELS = 16;

inline int effective_channels(int channels)
{
    if (channels < 1) return 1;
    return channels > MAX_CHANNELS ? MAX_CHANNELS : channels;
}

inline int broadcast_lane(int lane, int channels)
{
    const int count = effective_channels(channels);
    if (count == 1 || lane >= count) return 0;
    return lane;
}

template <typename ResetLane>
inline void reset_changed_lanes(int previousChannels,
                                int currentChannels,
                                ResetLane resetLane)
{
    const int begin = previousChannels < currentChannels
        ? previousChannels : currentChannels;
    const int end = previousChannels > currentChannels
        ? previousChannels : currentChannels;
    for (int lane = begin; lane < end; ++lane)
        resetLane(lane);
}

} // namespace polyphony
} // namespace wintoid
