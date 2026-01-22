#ifndef EGIAMP_AMPSERVERPROTOCOL_H
#define EGIAMP_AMPSERVERPROTOCOL_H

#include <cstdint>

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
        default:
            return 1.0f;
    }
}

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
