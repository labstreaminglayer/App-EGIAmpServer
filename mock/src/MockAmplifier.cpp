#include "MockAmplifier.h"
#include <cmath>
#include <cstring>

namespace mock {

MockAmplifier::MockAmplifier(int64_t id) {
    state_.ampId = id;
    state_.startTime = getCurrentTimeMicros();

    // Initialize PIB gains to 1
    for (int i = 0; i < 32; ++i) {
        state_.pibChannelGain[i] = 1;
    }
}

int64_t MockAmplifier::getCurrentTimeMicros() const {
    auto now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(epoch).count();
}

bool MockAmplifier::setPower(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.powered = on;
    if (on) {
        state_.startTime = getCurrentTimeMicros();
        state_.packetCounter = 0;
    } else {
        state_.streaming = false;
    }
    return true;
}

bool MockAmplifier::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.powered) {
        return false;
    }
    state_.streaming = true;
    state_.startTime = getCurrentTimeMicros();
    state_.packetCounter = 0;
    return true;
}

bool MockAmplifier::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.streaming = false;
    return true;
}

bool MockAmplifier::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.streaming = false;
    state_.packetCounter = 0;
    setDefaultAcquisitionState();
    return true;
}

void MockAmplifier::setNetCode(NetCode code) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.netCode = code;
    state_.channelCount = getChannelCountFromNetCode(code);
}

void MockAmplifier::setAmplifierType(AmplifierType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.amplifierType = type;
}

void MockAmplifier::setSerialNumber(const std::string& serial) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.serialNumber = serial;
}

void MockAmplifier::setFirmwareVersion(const std::string& version) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.firmwareVersion = version;
}

bool MockAmplifier::setNativeRate(int rate) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Real amplifier validates native rate
    // NA400: 500, 1000, 2000, 4000, 8000
    // NA410: 20000 only
    if (state_.amplifierType == AmplifierType::NA410) {
        if (rate != 20000) return false;
    } else {
        if (rate != 500 && rate != 1000 && rate != 2000 &&
            rate != 4000 && rate != 8000) {
            return false;
        }
    }
    state_.nativeRate = rate;
    return true;
}

bool MockAmplifier::setDecimatedRate(int rate) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Real amplifier accepts any rate value without validation
    // Valid rates: 250, 500, 1000
    state_.decimatedRate = rate;
    return true;
}

void MockAmplifier::setAll10KOhms(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.all10KOhms = on;
    for (int i = 0; i < 288; ++i) {
        state_.channel10KOhms[i] = on;
    }
}

void MockAmplifier::setChannel10KOhms(int channel, bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (channel >= 0 && channel < 288) {
        state_.channel10KOhms[channel] = on;
    }
}

void MockAmplifier::setCom10KOhms(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.com10KOhms = on;
}

void MockAmplifier::setReference10KOhms(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.reference10KOhms = on;
}

void MockAmplifier::setAllDriveSignals(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.allDriveSignals = on;
    for (int i = 0; i < 288; ++i) {
        state_.channelDriveSignals[i] = on;
    }
}

void MockAmplifier::setChannelDriveSignals(int channel, bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (channel >= 0 && channel < 288) {
        state_.channelDriveSignals[channel] = on;
    }
}

void MockAmplifier::setComDriveSignal(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.comDriveSignal = on;
}

void MockAmplifier::setReferenceDriveSignal(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.referenceDriveSignal = on;
}

void MockAmplifier::setSubjectGround(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.subjectGround = on;
}

void MockAmplifier::setCurrentSource(int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.currentSource = value;
}

void MockAmplifier::setCalibrationSignalFreq(int freq) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.calibrationSignalFreq = freq;
}

void MockAmplifier::setBufferedReference(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.bufferedReference = on;
}

void MockAmplifier::setOscillatorGate(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.oscillatorGate = on;
}

void MockAmplifier::setWaveShape(WaveShape shape) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.waveShape = shape;
}

void MockAmplifier::setDrivenCommon(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.drivenCommon = on;
}

void MockAmplifier::setCalibrationSignalAmplitude(int amplitude) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.calibrationSignalAmplitude = amplitude;
}

void MockAmplifier::setDigitalOutputData(uint16_t data) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Mask input bits
    uint16_t outputMask = state_.digitalInOutDirection;
    state_.digitalOutputData = (state_.digitalOutputData & ~outputMask) | (data & outputMask);
}

void MockAmplifier::setDigitalInOutDirection(uint16_t direction) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.digitalInOutDirection = direction;
}

uint16_t MockAmplifier::getDigitalInputData() const {
    std::lock_guard<std::mutex> lock(mutex_);
    // Return mock digital input (could be randomized or pattern-based)
    return 0;
}

void MockAmplifier::setChannelZeroOhms(int channel, bool ground) {
    std::lock_guard<std::mutex> lock(mutex_);
    // In real hardware, this shorts the channel to ground
    // For mock, we just track the state (could affect generated data)
}

void MockAmplifier::setAllZeroOhms(bool ground) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Short all channels to ground
}

void MockAmplifier::setPIBChannelGain(int channel, int gain) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (channel >= 1 && channel <= 32) {
        state_.pibChannelGain[channel - 1] = gain;
    }
}

int MockAmplifier::getPhysioConnectionStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_.physioConnectionStatus;
}

void MockAmplifier::setPhysioConnectionStatus(int status) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.physioConnectionStatus = status;
}

int64_t MockAmplifier::getStartTime() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_.startTime;
}

uint64_t MockAmplifier::getPacketCounter() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_.packetCounter;
}

void MockAmplifier::incrementPacketCounter() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++state_.packetCounter;
}

// GTEN functions
bool MockAmplifier::setGTENTrain(const std::string& mcfXml) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Parse MCF XML and configure GTEN train
    // For mock, just accept it
    return true;
}

bool MockAmplifier::setGTENWaveforms(const std::string& mcfXml) {
    std::lock_guard<std::mutex> lock(mutex_);
    return true;
}

bool MockAmplifier::setGTENBlocks(const std::string& mcfXml) {
    std::lock_guard<std::mutex> lock(mutex_);
    return true;
}

bool MockAmplifier::startGTENTrain() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.powered) return false;
    state_.gtenTrainRunning = true;
    return true;
}

bool MockAmplifier::abortGTENTrain() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.gtenTrainRunning = false;
    return true;
}

bool MockAmplifier::resetGTENAlarm() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.gtenAlarm = false;
    return true;
}

void MockAmplifier::setDefaultAcquisitionState() {
    // Called without lock, assumes caller holds lock or is in constructor
    state_.all10KOhms = false;
    for (int i = 0; i < 288; ++i) {
        state_.channel10KOhms[i] = false;
        state_.channelDriveSignals[i] = false;
    }
    state_.allDriveSignals = false;
    state_.subjectGround = true;
    state_.currentSource = 0;
    state_.calibrationSignalFreq = 0;
    state_.bufferedReference = true;
    state_.oscillatorGate = false;
    state_.reference10KOhms = false;
    state_.referenceDriveSignal = false;
    state_.waveShape = WaveShape::Sine;
    state_.calibrationSignalAmplitude = 0;
    state_.drivenCommon = false;
}

void MockAmplifier::setDefaultSignalGenerationState() {
    state_.allDriveSignals = true;
    for (int i = 0; i < 288; ++i) {
        state_.channelDriveSignals[i] = true;
    }
    state_.subjectGround = false;
    state_.calibrationSignalFreq = 5;
    state_.bufferedReference = false;
    state_.oscillatorGate = true;
    state_.calibrationSignalAmplitude = 50;
}

void MockAmplifier::generatePacketFormat1(PacketFormat1_SamplePacket& packet) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::memset(&packet, 0, sizeof(packet));

    // Set header with net code
    packet.header[6] = (static_cast<uint32_t>(state_.netCode) << 16);

    // Generate synthetic EEG data (sine waves with some noise)
    double sampleRate = state_.decimatedRate;
    double dt = 1.0 / sampleRate;
    phase_ += dt;

    float scaleFactor = getScalingFactor(state_.amplifierType);

    for (int ch = 0; ch < 256; ++ch) {
        double freq = 10.0 + (ch % 40);  // Different frequencies per channel
        double amplitude = 50.0;  // ~50 uV amplitude
        double noise = (rand() % 1000 - 500) / 500.0 * 5.0;  // ~5 uV noise

        // Generate calibration signal if enabled
        if (state_.channelDriveSignals[ch] || state_.allDriveSignals) {
            freq = state_.calibrationSignalFreq > 0 ? state_.calibrationSignalFreq : 10.0;
            amplitude = state_.calibrationSignalAmplitude > 0 ?
                        state_.calibrationSignalAmplitude : 100.0;
        }

        double value = amplitude * std::sin(2.0 * M_PI * freq * phase_) + noise;
        packet.eeg[ch] = static_cast<float>(value / 1000000.0);  // Store in volts
    }

    // PIB data
    for (int i = 0; i < 7; ++i) {
        packet.pib[i] = 0.0f;
    }

    // Reference and common
    packet.ref = 0.0f;
    packet.com = 0.0f;

    ++state_.packetCounter;
}

void MockAmplifier::generatePacketFormat2(PacketFormat2_SamplePacket& packet) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::memset(&packet, 0, sizeof(packet));

    // Digital inputs (mock value)
    packet.digitalInputs = 0;

    // TR byte (255 = no GTEN activity)
    packet.tr = state_.gtenTrainRunning ? 251 : 255;

    // Packet counter and timestamp
    packet.packetCounter = state_.packetCounter;
    packet.timeStamp = getCurrentTimeMicros();

    // Net code
    packet.netCode = static_cast<uint8_t>(state_.netCode);

    // Generate synthetic EEG data
    double sampleRate = state_.decimatedRate;
    double dt = 1.0 / sampleRate;
    phase_ += dt;

    float scaleFactor = getScalingFactor(state_.amplifierType);

    for (int ch = 0; ch < 256; ++ch) {
        double freq = 10.0 + (ch % 40);
        double amplitude = 50.0;  // ~50 uV
        double noise = (rand() % 1000 - 500) / 500.0 * 5.0;

        if (state_.channelDriveSignals[ch] || state_.allDriveSignals) {
            freq = state_.calibrationSignalFreq > 0 ? state_.calibrationSignalFreq : 10.0;
            amplitude = state_.calibrationSignalAmplitude > 0 ?
                        state_.calibrationSignalAmplitude : 100.0;
        }

        double value = amplitude * std::sin(2.0 * M_PI * freq * phase_) + noise;

        // Convert to ADC units (reverse of scaling factor)
        packet.eegData[ch] = static_cast<int32_t>(value / scaleFactor);
    }

    // Aux data
    for (int i = 0; i < 3; ++i) {
        packet.auxData[i] = 0;
    }

    // Monitor channels
    packet.refMonitor = 0;
    packet.comMonitor = 0;
    packet.driveMonitor = 0;
    packet.diagnosticsChannel = 0;
    packet.currentSense = 0;

    // PIB data - generate test signals if physio is connected
    // Physio scaling factors: channels 1-8 use -0.00111758708, channels 9-16 use +0.00111758708
    constexpr double PHYSIO_SCALE_1_8 = -0.00111758708;
    constexpr double PHYSIO_SCALE_9_16 = 0.00111758708;

    for (int i = 0; i < 16; ++i) {
        double physioScale = (i < 8) ? PHYSIO_SCALE_1_8 : PHYSIO_SCALE_9_16;

        if (state_.physioConnectionStatus & 0x01) {
            // Port 1: Generate 7 Hz sine wave with channel-specific phase offset
            double pib1Freq = 7.0;
            double pib1Value = 50.0 * std::sin(2.0 * M_PI * pib1Freq * phase_ + i * 0.2);
            packet.pib1_Data[i] = static_cast<int32_t>(pib1Value / physioScale);
        } else {
            packet.pib1_Data[i] = 0;
        }

        if (state_.physioConnectionStatus & 0x02) {
            // Port 2: Generate 11 Hz sine wave with channel-specific phase offset
            double pib2Freq = 11.0;
            double pib2Value = 50.0 * std::sin(2.0 * M_PI * pib2Freq * phase_ + i * 0.2);
            packet.pib2_Data[i] = static_cast<int32_t>(pib2Value / physioScale);
        } else {
            packet.pib2_Data[i] = 0;
        }
    }

    ++state_.packetCounter;
}

} // namespace mock
