#ifndef EGIAMP_ELECTRODEPOSITIONS_H
#define EGIAMP_ELECTRODEPOSITIONS_H

#include <cstddef>

namespace egiamp {

// 3D electrode position (x, y, z in head coordinates from .sfp files)
// Coordinates follow standard EEG conventions:
//   X: positive toward right ear (T8)
//   Y: positive toward nose (Nasion)
//   Z: positive toward vertex (Cz)
struct ElectrodePosition {
    float x;
    float y;
    float z;
};

// Get electrode position for a given net size and channel index
// Returns nullptr if the channel index is out of range for the net size
const ElectrodePosition* getElectrodePosition(int netSize, int channelIndex);

// Get the full position array for a given net size
// Returns nullptr if the net size is not supported
const ElectrodePosition* getElectrodePositions(int netSize, size_t& count);

} // namespace egiamp

#endif // EGIAMP_ELECTRODEPOSITIONS_H
