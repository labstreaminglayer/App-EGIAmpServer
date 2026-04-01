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

    /// Derive LSL stream name from the server address.
    /// .51 → "EGINetAmp_51", .52 → "EGINetAmp_52", localhost/127.* → "EGINetAmp_mock".
    std::string streamName() const;

    /// Suffix for LSL source ID to distinguish native/decimated mode and timestamp alignment.
    /// Ensures LSL consumers won't auto-reconnect across incompatible configurations.
    std::string modeSuffix() const {
        const bool native = sampleRate > 1000 || (fastRecovery && sampleRate >= 500);
        std::string s = native ? "_native" : "_decimated";
        if (alignTimestamps && !native) s += "_aligned";
        return s;
    }

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
