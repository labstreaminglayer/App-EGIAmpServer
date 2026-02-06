#include "egiamp/ElectrodePositions.h"

#include <cmath>

namespace egiamp {

namespace {

// Helper to generate positions on a geodesic sphere
// Uses Fibonacci lattice for even distribution
constexpr float PI = 3.14159265358979323846f;

ElectrodePosition sphericalToCartesian(float theta, float phi) {
    // theta: angle from top (0 = vertex, PI = bottom)
    // phi: azimuthal angle (0 = front, PI/2 = right ear)
    float sinTheta = std::sin(theta);
    return {
        sinTheta * std::sin(phi),   // X: positive right
        sinTheta * std::cos(phi),   // Y: positive front (nose)
        std::cos(theta)             // Z: positive up (vertex)
    };
}

// Generate positions using Fibonacci lattice on a sphere
// This gives a good approximation of geodesic electrode placement
template<size_t N>
std::array<ElectrodePosition, N> generateGeodesicPositions() {
    std::array<ElectrodePosition, N> positions;

    // Golden ratio for Fibonacci lattice
    const float goldenRatio = (1.0f + std::sqrt(5.0f)) / 2.0f;
    const float goldenAngle = 2.0f * PI / (goldenRatio * goldenRatio);

    // Electrodes are typically placed on upper hemisphere with some neck coverage
    // EGI nets cover roughly from Cz down to just below ear level
    const float minZ = -0.3f;  // Lowest point (below ear level)
    const float maxZ = 1.0f;   // Vertex (Cz)

    for (size_t i = 0; i < N - 1; i++) {  // N-1 regular electrodes, last is Cz
        // Distribute evenly in Z (height), more dense near top
        float t = static_cast<float>(i) / static_cast<float>(N - 2);

        // Use square root distribution for more even spherical coverage
        float z = maxZ - (maxZ - minZ) * std::sqrt(t);

        // Azimuthal angle using golden angle for even distribution
        float phi = goldenAngle * static_cast<float>(i);

        // Convert to full 3D position
        float r = std::sqrt(1.0f - z * z);  // Radius at this height on unit sphere
        positions[i] = {
            r * std::sin(phi),   // X
            r * std::cos(phi),   // Y
            z                     // Z
        };
    }

    // Last electrode is always Cz at vertex
    positions[N - 1] = {0.0f, 0.0f, 1.0f};

    return positions;
}

} // anonymous namespace

// Pre-generated electrode positions
// Note: These are algorithmic approximations. For clinical accuracy,
// use the official EGI .sfp coordinate files for your specific net.

const std::array<ElectrodePosition, 257> GSN256_POSITIONS = generateGeodesicPositions<257>();
const std::array<ElectrodePosition, 129> GSN128_POSITIONS = generateGeodesicPositions<129>();
const std::array<ElectrodePosition, 65> GSN64_POSITIONS = generateGeodesicPositions<65>();
const std::array<ElectrodePosition, 33> GSN32_POSITIONS = generateGeodesicPositions<33>();

const ElectrodePosition* getElectrodePosition(int netSize, int channelIndex) {
    size_t count;
    const ElectrodePosition* positions = getElectrodePositions(netSize, count);
    if (positions && channelIndex >= 0 && static_cast<size_t>(channelIndex) < count) {
        return &positions[channelIndex];
    }
    return nullptr;
}

const ElectrodePosition* getElectrodePositions(int netSize, size_t& count) {
    switch (netSize) {
        case 256:
        case 257:
            count = 257;
            return GSN256_POSITIONS.data();
        case 128:
        case 129:
            count = 129;
            return GSN128_POSITIONS.data();
        case 64:
        case 65:
            count = 65;
            return GSN64_POSITIONS.data();
        case 32:
        case 33:
            count = 33;
            return GSN32_POSITIONS.data();
        default:
            count = 0;
            return nullptr;
    }
}

} // namespace egiamp
