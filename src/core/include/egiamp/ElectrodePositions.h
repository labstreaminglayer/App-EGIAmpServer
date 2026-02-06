#ifndef EGIAMP_ELECTRODEPOSITIONS_H
#define EGIAMP_ELECTRODEPOSITIONS_H

#include <array>
#include <cstddef>

namespace egiamp {

// 3D electrode position (x, y, z in normalized head coordinates)
// Coordinates follow standard EEG conventions:
//   X: positive toward right ear (T8)
//   Y: positive toward nose (Nasion)
//   Z: positive toward vertex (Cz)
// Positions are normalized to a unit sphere centered at origin
struct ElectrodePosition {
    float x;
    float y;
    float z;
};

// GSN HydroCel 256-channel electrode positions
// These are approximate positions based on geodesic sensor net geometry
// Electrode E257 (index 256) is Cz when present as a recorded channel
extern const std::array<ElectrodePosition, 257> GSN256_POSITIONS;

// GSN HydroCel 128-channel electrode positions
// Electrode E129 (index 128) is Cz when present as a recorded channel
extern const std::array<ElectrodePosition, 129> GSN128_POSITIONS;

// GSN HydroCel 64-channel electrode positions
// Electrode E65 (index 64) is Cz when present as a recorded channel
extern const std::array<ElectrodePosition, 65> GSN64_POSITIONS;

// GSN HydroCel 32-channel electrode positions
// Electrode E33 (index 32) is Cz when present as a recorded channel
extern const std::array<ElectrodePosition, 33> GSN32_POSITIONS;

// Get electrode position for a given net size and channel index
// Returns nullptr if the channel index is out of range for the net size
const ElectrodePosition* getElectrodePosition(int netSize, int channelIndex);

// Get the full position array for a given net size
// Returns nullptr if the net size is not supported
const ElectrodePosition* getElectrodePositions(int netSize, size_t& count);

} // namespace egiamp

#endif // EGIAMP_ELECTRODEPOSITIONS_H
