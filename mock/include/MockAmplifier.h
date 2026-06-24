#ifndef MOCK_AMPLIFIER_H
#define MOCK_AMPLIFIER_H

#include "AmpServerProtocol.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace mock {

struct AmplifierState {
    // Power and acquisition state
    bool powered = false;
    bool streaming = false;

    // Amplifier identification
    int64_t ampId = 0;
    std::string serialNumber = "MOCK12345678";
    std::string firmwareVersion = "2.0.16";
    AmplifierType amplifierType = AmplifierType::NA400;
    PacketFormat packetFormat = PacketFormat::Format2;
    NetCode netCode = NetCode::GSN256_2_0;
    int channelCount = 256;
    bool legacyBoard = false;

    // Sampling configuration
    int nativeRate = 1000;
    int decimatedRate = 1000;
    bool decimated = true;  // true after cmd_SetDecimatedRate (FPGA filter on),
                            // false after cmd_SetNativeRate (no filter, no skew)

    // Active streaming rate = whichever mode was last selected.
    int activeRate() const {
        int r = decimated ? decimatedRate : nativeRate;
        return r > 0 ? r : 1000;
    }

    // Calibration/test signal state
    bool all10KOhms = false;
    bool channel10KOhms[288] = {false};
    bool com10KOhms = false;
    bool reference10KOhms = false;

    bool allDriveSignals = false;
    bool channelDriveSignals[288] = {false};
    bool comDriveSignal = false;
    bool referenceDriveSignal = false;

    bool subjectGround = true;
    bool bufferedReference = true;
    bool oscillatorGate = false;
    bool drivenCommon = false;

    WaveShape waveShape = WaveShape::Sine;
    int calibrationSignalFreq = 0;
    int calibrationSignalAmplitude = 0;
    int currentSource = 0;

    // Digital I/O
    uint16_t digitalOutputData = 0;
    uint16_t digitalInOutDirection = 0; // 0 = input, 1 = output

    // Physio16 state
    int physioConnectionStatus = 0; // 0=none, 1=port1, 2=port2, 3=both
    int pibChannelGain[32] = {1};

    // Timing
    int64_t startTime = 0;  // Microseconds since epoch
    uint64_t packetCounter = 0;

    // GTEN state (for GTEN 200)
    bool gtenTrainRunning = false;
    bool gtenAlarm = false;
    int gtenRemainingTimeMs = 0;
    std::string gtenWaveformType = "DC";
    bool gtenWaveformPulsed = false;
};

class MockAmplifier {
public:
    MockAmplifier(int64_t id = 0);
    ~MockAmplifier() = default;

    // Prevent copying
    MockAmplifier(const MockAmplifier&) = delete;
    MockAmplifier& operator=(const MockAmplifier&) = delete;

    // Amplifier control
    bool setPower(bool on);
    bool start();
    bool stop();
    bool reset();

    // Configuration
    void setNetCode(NetCode code);
    void setAmplifierType(AmplifierType type);
    void setSerialNumber(const std::string& serial);
    void setFirmwareVersion(const std::string& version);

    // Sampling rate
    bool setNativeRate(int rate);
    bool setDecimatedRate(int rate);

    // 10K Ohm resistor control
    void setAll10KOhms(bool on);
    void setChannel10KOhms(int channel, bool on);
    void setCom10KOhms(bool on);
    void setReference10KOhms(bool on);

    // Drive signal control
    void setAllDriveSignals(bool on);
    void setChannelDriveSignals(int channel, bool on);
    void setComDriveSignal(bool on);
    void setReferenceDriveSignal(bool on);

    // Other configuration
    void setSubjectGround(bool on);
    void setCurrentSource(int value);
    void setCalibrationSignalFreq(int freq);
    void setBufferedReference(bool on);
    void setOscillatorGate(bool on);
    void setWaveShape(WaveShape shape);
    void setDrivenCommon(bool on);
    void setCalibrationSignalAmplitude(int amplitude);

    // Digital I/O
    void setDigitalOutputData(uint16_t data);
    void setDigitalInOutDirection(uint16_t direction);
    uint16_t getDigitalInputData() const;

    // Zero ohms (ground channel)
    void setChannelZeroOhms(int channel, bool ground);
    void setAllZeroOhms(bool ground);

    // PIB/Physio16
    void setPIBChannelGain(int channel, int gain);
    int getPhysioConnectionStatus() const;
    void setPhysioConnectionStatus(int status);

    // Queries
    int64_t getStartTime() const;
    uint64_t getPacketCounter() const;
    void incrementPacketCounter();

    // GTEN control
    bool setGTENTrain(const std::string& mcfXml);
    bool setGTENWaveforms(const std::string& mcfXml);
    bool setGTENBlocks(const std::string& mcfXml);
    bool startGTENTrain();
    bool abortGTENTrain();
    bool resetGTENAlarm();

    // State access
    const AmplifierState& state() const { return state_; }
    bool isPowered() const { return state_.powered; }
    bool isStreaming() const { return state_.streaming; }
    int64_t ampId() const { return state_.ampId; }

    // Generate sample data
    void generatePacketFormat1(PacketFormat1_SamplePacket& packet);
    void generatePacketFormat2(PacketFormat2_SamplePacket& packet);

    // Default acquisition/signal generation states
    void setDefaultAcquisitionState();
    void setDefaultSignalGenerationState();

private:
    AmplifierState state_;
    mutable std::mutex mutex_;

    // For synthetic data generation
    double phase_ = 0.0;

    int64_t getCurrentTimeMicros() const;
};

} // namespace mock

#endif // MOCK_AMPLIFIER_H
