#include "egiamp/ElectrodePositions.h"
#include "egiamp/EmbeddedResources.h"

#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace egiamp {

namespace {

// Parse an .sfp file into a vector of ElectrodePosition.
// Skips Fid* lines, parses E{N} and Cz entries.
// Returns positions indexed 0..N-1 for E1..EN, with Cz appended as last element.
std::vector<ElectrodePosition> parseSfpData(std::string_view data) {
    std::vector<ElectrodePosition> positions;

    // First pass: find max electrode number to size the vector
    int maxE = 0;
    bool hasCz = false;
    {
        std::istringstream iss{std::string(data)};
        std::string label;
        float x, y, z;
        while (iss >> label >> x >> y >> z) {
            if (label.size() > 1 && label[0] == 'E') {
                int num = std::stoi(label.substr(1));
                if (num > maxE) maxE = num;
            } else if (label == "Cz") {
                hasCz = true;
            }
        }
    }

    int totalSize = hasCz ? maxE + 1 : maxE;
    positions.resize(totalSize);

    // Second pass: store positions
    {
        std::istringstream iss{std::string(data)};
        std::string label;
        float x, y, z;
        while (iss >> label >> x >> y >> z) {
            if (label.size() > 1 && label[0] == 'E') {
                int num = std::stoi(label.substr(1));
                if (num >= 1 && num <= maxE) {
                    positions[num - 1] = {x, y, z};
                }
            } else if (label == "Cz" && hasCz) {
                positions[maxE] = {x, y, z};
            }
        }
    }

    return positions;
}

struct PositionCache {
    std::mutex mutex;
    std::map<int, std::vector<ElectrodePosition>> entries;
};

PositionCache& getCache() {
    static PositionCache cache;
    return cache;
}

const std::vector<ElectrodePosition>* lookupOrParse(int base) {
    auto& cache = getCache();
    std::lock_guard<std::mutex> lock(cache.mutex);

    auto it = cache.entries.find(base);
    if (it != cache.entries.end()) {
        return &it->second;
    }

    // Map base net size to the appropriate montage resource.
    // We always use the extended variant (includes Cz) so the array
    // covers both base and base+1 net sizes.
    std::string_view sfp;
    switch (base) {
        case 32:  sfp = resources::montage_32;  break;  // 32 E-channels + Cz = 33 positions
        case 64:  sfp = resources::montage_65;  break;
        case 128: sfp = resources::montage_129; break;
        case 256: sfp = resources::montage_257; break;
        default: return nullptr;
    }

    auto [inserted, _] = cache.entries.emplace(base, parseSfpData(sfp));
    return &inserted->second;
}

} // anonymous namespace

const ElectrodePosition* getElectrodePosition(int netSize, int channelIndex) {
    size_t count;
    const ElectrodePosition* positions = getElectrodePositions(netSize, count);
    if (positions && channelIndex >= 0 && static_cast<size_t>(channelIndex) < count) {
        return &positions[channelIndex];
    }
    return nullptr;
}

const ElectrodePosition* getElectrodePositions(int netSize, size_t& count) {
    int base;
    switch (netSize) {
        case 256: case 257: base = 256; break;
        case 128: case 129: base = 128; break;
        case 64:  case 65:  base = 64;  break;
        case 32:  case 33:  base = 32;  break;
        default:
            count = 0;
            return nullptr;
    }

    const auto* vec = lookupOrParse(base);
    if (!vec || vec->empty()) {
        count = 0;
        return nullptr;
    }

    count = vec->size();
    return vec->data();
}

} // namespace egiamp
