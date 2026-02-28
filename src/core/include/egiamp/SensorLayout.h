#ifndef EGIAMP_SENSORLAYOUT_H
#define EGIAMP_SENSORLAYOUT_H

#include <string_view>
#include <vector>

namespace egiamp {

struct SensorPosition2D {
    float x, y;
};

struct TilingSet {
    std::vector<int> channels;  // 0-based channel indices
    bool isReference = false;   // true for the VREF-only set
};

class SensorLayout {
public:
    bool load(std::string_view xml);

    const SensorPosition2D* getPosition2D(int ch) const;
    const std::vector<TilingSet>& tilingSets() const { return tilingSets_; }
    int electrodeCount() const { return electrodeCount_; }

    // Lazy-cached lookup by net size (32/33→32, 64/65→64, etc.)
    static const SensorLayout* forNetSize(int netSize);

private:
    std::vector<SensorPosition2D> positions_;  // indexed by 0-based channel
    std::vector<TilingSet> tilingSets_;
    int electrodeCount_ = 0;
    int vrefNumber_ = -1;  // 1-based VREF sensor number
};

} // namespace egiamp

#endif // EGIAMP_SENSORLAYOUT_H
