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
    // Amount the Physio16 (PNS/PIB) stream is delayed to realign it with the
    // FPGA-filtered EEG in decimated mode. The PIB path leads the EEG by a fixed
    // 33 ms physical delay (measured 300 s DIN+EEG+Physio16 sweep, corroborated
    // across device versions; see scripts/delay_capture_sweep.py +
    // analyze_delay_sweep.py). The applied delay is floor(33 ms x rate) samples,
    // which the device quantizes to 8 @ 250 Hz and 16 @ 500 Hz (both 32 ms) and
    // 33 @ 1000 Hz. Applied only in decimated mode; ignored for native rates.
    int physioAlignDelayMs = 33;
    bool impedance = false;
    bool nativeFormat = false;  // When true, transmit raw int32 ADC counts instead of float microvolts

    /// Derive LSL stream name from the server address.
    /// .51 → "EGINetAmp_51", .52 → "EGINetAmp_52", localhost/127.* → "EGINetAmp_mock".
    std::string streamName() const;

    /// Suffix for LSL source ID to distinguish native/decimated mode and timestamp alignment.
    /// Ensures LSL consumers won't auto-reconnect across incompatible configurations.
    std::string modeSuffix() const {
        const bool native = sampleRate > 1000 || (fastRecovery && sampleRate >= 500);
        // Decimated streams are always alignment-corrected, so "_decimated"
        // already implies aligned — no separate suffix needed.
        return native ? "_native" : "_decimated";
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
