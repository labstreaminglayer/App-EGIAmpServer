#ifndef EGIAMP_AMPSERVERPROTOCOL_H
#define EGIAMP_AMPSERVERPROTOCOL_H

#include <cstdint>
#include <initializer_list>

namespace egiamp {

// Number of samples per chunk sent into LSL
constexpr int SAMPLES_PER_CHUNK = 32;

// =============================================================================
// Enums
// =============================================================================

enum class PacketType : int {
    Format1 = 1,
    Format2 = 2
};

enum class AmplifierType : int {
    NA300 = 0,
    NA400 = 1,
    NA410 = 2,
    NA500 = 3,
    Unknown = 4
};

enum class NetCode : uint8_t {
    GSN64_2_0 = 0,      // GSN 64
    GSN128_2_0 = 1,     // GSN 128
    GSN256_2_0 = 2,     // GSN 256

    HCGSN32_1_0 = 3,    // HGSN 32
    HCGSN64_1_0 = 4,    // HGSN 64
    HCGSN128_1_0 = 5,   // HGSN 128
    HCGSN256_1_0 = 6,   // HGSN 256

    MCGSN32_1_0 = 7,    // MGSN 32
    MCGSN64_1_0 = 8,    // MGSN 64
    MCGSN128_1_0 = 9,   // MGSN 128
    MCGSN256_1_0 = 10,  // MGSN 256

    TestConnector = 14,
    NoNet = 15,         // Net not connected
    Unknown = 0xFF      // Unknown or net not connected
};

// =============================================================================
// Binary Protocol Structures (must be packed for wire format)
// =============================================================================

#pragma pack(push, 1)

struct PacketFormat1 {
    uint32_t header[8]; // DINS (Digital Inputs) 1-8/9-16 at bytes 24/25; net type at byte 26
    float eeg[256];     // EEG Data
    float pib[7];       // PIB data
    float unused1;      // N/A
    float ref;          // The reference channel
    float com;          // The common channel
    float unused2;      // N/A
    float padding[13];  // N/A
};

struct PacketFormat2_PIB_AUX {
    uint8_t digitalInputs;
    uint8_t status;
    uint8_t batteryLevel[3];
    uint8_t temperature[3];
    uint8_t sp02;
    uint8_t heartRate[2];
};

struct PacketFormat2 {
    uint16_t digitalInputs;
    uint8_t tr;
    PacketFormat2_PIB_AUX pib1_aux;
    PacketFormat2_PIB_AUX pib2_aux;
    uint64_t packetCounter;
    uint64_t timeStamp;
    uint8_t netCode;
    uint8_t reserved[38];
    int32_t eegData[256];
    int32_t auxData[3];
    int32_t refMonitor;
    int32_t comMonitor;
    int32_t driveMonitor;
    int32_t diagnosticsChannel;
    int32_t currentSense;
    int32_t pib1_Data[16];
    int32_t pib2_Data[16];
};

struct AmpDataPacketHeader {
    int64_t ampID;      // The ampID associated with this data packet
    uint64_t length;    // Specifies the length of the data field
};

#pragma pack(pop)

// =============================================================================
// Utility Functions
// =============================================================================

inline int getChannelCountFromNetCode(NetCode code) {
    switch (code) {
        case NetCode::HCGSN32_1_0:
        case NetCode::MCGSN32_1_0:
            return 32;

        case NetCode::GSN64_2_0:
        case NetCode::HCGSN64_1_0:
        case NetCode::MCGSN64_1_0:
            return 64;

        case NetCode::GSN128_2_0:
        case NetCode::HCGSN128_1_0:
        case NetCode::MCGSN128_1_0:
            return 128;

        case NetCode::GSN256_2_0:
        case NetCode::HCGSN256_1_0:
        case NetCode::MCGSN256_1_0:
            return 256;

        default:
            return 0;
    }
}

inline float getScalingFactor(AmplifierType type) {
    switch (type) {
        case AmplifierType::NA300:
            return 0.0244140625f;
        case AmplifierType::NA400:
            return 0.00015522042f;
        case AmplifierType::NA410:
            return 0.00009636188f;
        case AmplifierType::NA500:
            // 0.75 V total input range over 2^24 codes (24-bit), in microvolts.
            // Extracted from Net Station 6.0.0.60960's
            // libEgiDataSource.dll PhysicalAmp::NA500ScaleFactor(), which
            // computes exactly rangeVolts(0.75) / 2^24 * 1e6.
            return 750000.0f / 16777216.0f;  // 0.044703484 uV per count
        default:
            return 1.0f;
    }
}

// Rate/mode capabilities per model, verified against hardware where possible.
// NA400 (measured): decimated 250/500/1000; native 500/1000/2000/4000/8000.
// NA500 (measured, fw 0.4.9): decimated 250/500/1000/2000; native
// 250/500/1000/2000/4000. It accepts 8000 and 16000 but delivers at most ~7995
// samples/s, silently dropping the rest, so neither is offered. Unlike the NA400, the NA500 never sends
// duplicate packets at sub-1000 Hz decimated rates.
// Digitization-to-client latency (ms) subtracted from every timestamp when the
// config does not set one, a little under the value measured with
// scripts/audio_latency_test so a sample is never back-dated before its event.
// NA400: ~5 ms. NA500: ~1.0 ms + 1.1 sample periods (5.4 ms at 250 Hz down to
// 1.0 ms at 8 kHz), so its default follows the rate.
inline double defaultSystemDelayMs(AmplifierType type, int sampleRate) {
    if (type == AmplifierType::NA500 && sampleRate > 0) {
        return 0.8 + 1000.0 / sampleRate;
    }
    return 4.5;
}

inline int maxDecimatedRate(AmplifierType type) {
    return type == AmplifierType::NA500 ? 2000 : 1000;
}

inline int minNativeRate(AmplifierType type) {
    return type == AmplifierType::NA500 ? 250 : 500;
}

// Whether the given configuration runs the amp in native mode (no FPGA
// anti-alias filter) as opposed to decimated mode. Rates above the model's
// decimated maximum are necessarily native; otherwise fastRecovery selects
// native where the model supports it.
inline bool usesNativeRate(AmplifierType type, int sampleRate, bool fastRecovery) {
    return sampleRate > maxDecimatedRate(type) ||
           (fastRecovery && sampleRate >= minNativeRate(type));
}

// Whether the model supports the (rate, fastRecovery) combination, in the
// mode usesNativeRate() would select for it. Unknown models are permissive.
inline bool supportsSampleRate(AmplifierType type, int sampleRate, bool fastRecovery) {
    auto contains = [sampleRate](std::initializer_list<int> rates) {
        for (int r : rates) if (r == sampleRate) return true;
        return false;
    };
    const bool native = usesNativeRate(type, sampleRate, fastRecovery);
    switch (type) {
        case AmplifierType::NA300:
            // Decimated only; no native mode exists.
            return !native && contains({125, 250, 500, 1000});
        case AmplifierType::NA400:
            return native ? contains({500, 1000, 2000, 4000, 8000})
                          : contains({250, 500, 1000});
        case AmplifierType::NA410:
            return native ? contains({20000})
                          : contains({250, 500, 1000});
        case AmplifierType::NA500:
            // Verified on hardware (fw 0.4.9)
            return native ? contains({250, 500, 1000, 2000, 4000})
                          : contains({250, 500, 1000, 2000});
        default:
            return true;
    }
}

// Physio16 scaling factors (from EGI documentation)
// Channels 1-8 use negative scaling, channels 9-16 use positive scaling
constexpr float PHYSIO_SCALING_1_8 = -0.00111758708f;   // PIB channels 1-8
constexpr float PHYSIO_SCALING_9_16 = 0.00111758708f;   // PIB channels 9-16

inline const char* amplifierTypeName(AmplifierType type) {
    switch (type) {
        case AmplifierType::NA300:
            return "Net Amps 300";
        case AmplifierType::NA400:
            return "Net Amps 400";
        case AmplifierType::NA410:
            return "Net Amps 410";
        case AmplifierType::NA500:
            return "Net Amps 500";
        default:
            return "Unknown";
    }
}

inline const char* netCodeName(NetCode code) {
    switch (code) {
        case NetCode::GSN64_2_0:
            return "GSN-64 2.0";
        case NetCode::GSN128_2_0:
            return "GSN-128 2.0";
        case NetCode::GSN256_2_0:
            return "GSN-256 2.0";
        case NetCode::HCGSN32_1_0:
            return "HydroCel GSN-32 1.0";
        case NetCode::HCGSN64_1_0:
            return "HydroCel GSN-64 1.0";
        case NetCode::HCGSN128_1_0:
            return "HydroCel GSN-128 1.0";
        case NetCode::HCGSN256_1_0:
            return "HydroCel GSN-256 1.0";
        case NetCode::MCGSN32_1_0:
            return "MicroCel GSN-32 1.0";
        case NetCode::MCGSN64_1_0:
            return "MicroCel GSN-64 1.0";
        case NetCode::MCGSN128_1_0:
            return "MicroCel GSN-128 1.0";
        case NetCode::MCGSN256_1_0:
            return "MicroCel GSN-256 1.0";
        case NetCode::TestConnector:
            return "Test Connector";
        case NetCode::NoNet:
            return "No Net Connected";
        default:
            return "Unknown";
    }
}

} // namespace egiamp

#endif // EGIAMP_AMPSERVERPROTOCOL_H
