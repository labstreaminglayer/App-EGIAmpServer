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
    bool impedance = false;

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
