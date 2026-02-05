#ifndef EGIAMP_AMPSERVERCONFIG_H
#define EGIAMP_AMPSERVERCONFIG_H

#include <cstdint>
#include <string>
#include <stdexcept>

namespace egiamp {

struct AmpServerConfig {
    std::string serverAddress = "10.10.10.51";
    uint16_t commandPort = 9877;
    uint16_t notificationPort = 9878;
    uint16_t dataPort = 9879;
    int amplifierId = 0;
    int sampleRate = 1000;
    bool forceSampleRate = false;  // When true, reinitialize amp if running at different rate
    bool fastRecovery = false;     // When true, use native rate (no FPGA filter) for lower latency
    bool alignTimestamps = false;  // When true, adjust timestamps to compensate for filter delay
    bool impedance = false;
    bool nativeFormat = false;  // When true, transmit raw int32 ADC counts instead of float microvolts

    static AmpServerConfig loadFromFile(const std::string& filename);
    void saveToFile(const std::string& filename) const;
};

class ConfigError : public std::runtime_error {
public:
    explicit ConfigError(const std::string& message)
        : std::runtime_error(message) {}
};

} // namespace egiamp

#endif // EGIAMP_AMPSERVERCONFIG_H
