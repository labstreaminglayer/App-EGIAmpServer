#include "egiamp/SensorLayout.h"
#include "egiamp/EmbeddedResources.h"

#include <pugixml.hpp>

#include <map>
#include <mutex>
#include <sstream>

namespace egiamp {

bool SensorLayout::load(std::string_view xml) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_buffer(xml.data(), xml.size());
    if (!result) {
        return false;
    }

    // Navigate to root <sensorLayout> — pugixml ignores default namespace prefixes,
    // so unqualified child() calls work with the default xmlns.
    pugi::xml_node root = doc.child("sensorLayout");
    if (!root) {
        return false;
    }

    // Parse sensors
    pugi::xml_node sensors = root.child("sensors");
    electrodeCount_ = 0;
    vrefNumber_ = -1;

    // First pass: determine max electrode number for sizing
    int maxNumber = 0;
    for (pugi::xml_node sensor = sensors.child("sensor"); sensor; sensor = sensor.next_sibling("sensor")) {
        int number = sensor.child("number").text().as_int(0);
        int type = sensor.child("type").text().as_int(-1);
        if (type == 0 && number > maxNumber) {
            maxNumber = number;
        }
    }

    positions_.resize(maxNumber);
    electrodeCount_ = maxNumber;

    // Second pass: store positions and find VREF
    for (pugi::xml_node sensor = sensors.child("sensor"); sensor; sensor = sensor.next_sibling("sensor")) {
        int number = sensor.child("number").text().as_int(0);
        int type = sensor.child("type").text().as_int(-1);
        float x = sensor.child("x").text().as_float(0.0f);
        float y = sensor.child("y").text().as_float(0.0f);

        if (type == 0 && number >= 1 && number <= maxNumber) {
            positions_[number - 1] = {x, y};
        } else if (type == 1) {
            vrefNumber_ = number;
        }
    }

    // Parse tiling sets
    pugi::xml_node tilingSetsNode = root.child("tilingSets");
    tilingSets_.clear();

    for (pugi::xml_node tsNode = tilingSetsNode.child("tilingSet"); tsNode;
         tsNode = tsNode.next_sibling("tilingSet")) {
        TilingSet ts;
        std::istringstream iss(tsNode.text().as_string(""));
        int ch;
        while (iss >> ch) {
            if (ch == vrefNumber_) {
                ts.isReference = true;
            } else {
                ts.channels.push_back(ch - 1);  // Convert 1-based to 0-based
            }
        }
        tilingSets_.push_back(std::move(ts));
    }

    return true;
}

const SensorPosition2D* SensorLayout::getPosition2D(int ch) const {
    if (ch >= 0 && ch < static_cast<int>(positions_.size())) {
        return &positions_[ch];
    }
    return nullptr;
}

const SensorLayout* SensorLayout::forNetSize(int netSize) {
    // Normalize to base net size
    int base;
    switch (netSize) {
        case 32: case 33: base = 32; break;
        case 64: case 65: base = 64; break;
        case 128: case 129: base = 128; break;
        case 256: case 257: base = 256; break;
        default: return nullptr;
    }

    static std::mutex mutex;
    static std::map<int, SensorLayout> cache;

    std::lock_guard<std::mutex> lock(mutex);
    auto it = cache.find(base);
    if (it != cache.end()) {
        return &it->second;
    }

    std::string_view xml;
    switch (base) {
        case 32:  xml = resources::tiling_32;  break;
        case 64:  xml = resources::tiling_64;  break;
        case 128: xml = resources::tiling_128; break;
        case 256: xml = resources::tiling_256; break;
        default: return nullptr;
    }

    SensorLayout layout;
    if (!layout.load(xml)) {
        return nullptr;
    }

    auto [inserted, _] = cache.emplace(base, std::move(layout));
    return &inserted->second;
}

} // namespace egiamp
