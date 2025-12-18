#ifndef MOCK_AMPSERVERPROTOCOL_H
#define MOCK_AMPSERVERPROTOCOL_H

#include <cstdint>
#include <string>

namespace mock {

// Network ports
constexpr uint16_t COMMAND_PORT = 9877;
constexpr uint16_t NOTIFICATION_PORT = 9878;
constexpr uint16_t DATA_PORT = 9879;
constexpr uint16_t ECI_PORT = 55513;

// Packet format sizes
constexpr size_t PACKET_FORMAT1_SIZE = 1152;
constexpr size_t PACKET_FORMAT2_SIZE = 1264;
constexpr size_t DATA_HEADER_SIZE = 16;

// Amplifier types
enum class AmplifierType : int {
    NA300 = 0,
    NA400 = 1,
    NA410 = 2,
    NA500 = 3,
    GTEN200 = 4,
    Unknown = 5
};

// Net codes
enum class NetCode : uint8_t {
    GSN64_2_0 = 0,
    GSN128_2_0 = 1,
    GSN256_2_0 = 2,
    HCGSN32_1_0 = 3,
    HCGSN64_1_0 = 4,
    HCGSN128_1_0 = 5,
    HCGSN256_1_0 = 6,
    MCGSN32_1_0 = 7,
    MCGSN64_1_0 = 8,
    MCGSN128_1_0 = 9,
    MCGSN256_1_0 = 10,
    TestConnector = 14,
    NoNet = 15,
    Unknown = 0xFF
};

// Packet format types
enum class PacketFormat : int {
    Format1 = 1,
    Format2 = 2
};

// Waveform shapes
enum class WaveShape : int {
    Sine = 0,
    Square = 1,
    Triangle = 2,
    Sawtooth = 3
};

#pragma pack(push, 1)

// PIB Aux structure for Packet Format 2
struct PacketFormat2_PIB_AUX {
    uint8_t digitalInputs;
    uint8_t status;
    uint8_t batteryLevel[3];
    uint8_t temperature[3];
    uint8_t sp02;
    uint8_t heartRate[2];
};

// Packet Format 2 structure
struct PacketFormat2_SamplePacket {
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

// Packet Format 1 structure
struct PacketFormat1_SamplePacket {
    uint32_t header[8];
    float eeg[256];
    float pib[7];
    float unused1;
    float ref;
    float com;
    float unused2;
    float padding[13];
};

// Data stream header
struct AmpDataPacketHeader {
    int64_t ampID;
    uint64_t length;
};

#pragma pack(pop)

// Utility functions
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
        case NetCode::TestConnector:
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
        case AmplifierType::GTEN200:
            return 0.000155220429f;
        case AmplifierType::NA410:
            return 0.00009636188f;
        default:
            return 1.0f;
    }
}

inline const char* amplifierTypeName(AmplifierType type) {
    switch (type) {
        case AmplifierType::NA300:
            return "NA300";
        case AmplifierType::NA400:
            return "NA400";
        case AmplifierType::NA410:
            return "NA410";
        case AmplifierType::NA500:
            return "NA500";
        default:
            return "Unknown";
    }
}

inline const char* netCodeName(NetCode code) {
    switch (code) {
        case NetCode::GSN64_2_0: return "GSN64_2_0";
        case NetCode::GSN128_2_0: return "GSN128_2_0";
        case NetCode::GSN256_2_0: return "GSN256_2_0";
        case NetCode::HCGSN32_1_0: return "HCGSN32_1_0";
        case NetCode::HCGSN64_1_0: return "HCGSN64_1_0";
        case NetCode::HCGSN128_1_0: return "HCGSN128_1_0";
        case NetCode::HCGSN256_1_0: return "HCGSN256_1_0";
        case NetCode::MCGSN32_1_0: return "MCGSN32_1_0";
        case NetCode::MCGSN64_1_0: return "MCGSN64_1_0";
        case NetCode::MCGSN128_1_0: return "MCGSN128_1_0";
        case NetCode::MCGSN256_1_0: return "MCGSN256_1_0";
        case NetCode::TestConnector: return "TestConnector";
        case NetCode::NoNet: return "NoNet";
        default: return "Unknown";
    }
}

} // namespace mock

#endif // MOCK_AMPSERVERPROTOCOL_H
