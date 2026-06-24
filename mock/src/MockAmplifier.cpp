#include "MockAmplifier.h"
#include <cmath>
#include <cstring>

namespace mock {

namespace {
// DIN->EEG anti-alias filter group delay (ms) in decimated mode. Mirrors
// getFilterDelaySeconds() in the app (measured values). Native mode = no filter.
int filterDelayMs(int rate) {
    switch (rate) {
        case 250:  return 111;
        case 500:  return 61;
        case 1000: return 36;
        default:   return 0;
    }
}

// The PIB/physio path leads the FPGA-filtered EEG by this fixed physical delay in
// decimated mode (see notebooks/delay_inspection.ipynb). Quantized to whole
// samples at the active rate, matching the device (and the app's PhysioDelayLine).
constexpr int PHYSIO_LEAD_MS = 33;

// Common reference event injected into DIN pin-1 / EEG ch1 / physio ch1 (emulates
// the test jig). One pulse per second, 250 ms wide.
constexpr double REF_PERIOD_S = 1.0;
constexpr double REF_PULSE_S = 0.25;
constexpr double REF_EEG_PULSE_UV = 500.0;     // EEG ch1 deflection during the pulse
constexpr double REF_PHYSIO_PULSE_UV = 1000.0; // physio ch1 deflection during the pulse

// Is the reference pulse active at sample index k (handles negative k from delays)?
bool refActiveAtSample(long long k, int sampleRate) {
    const long long period = static_cast<long long>(REF_PERIOD_S * sampleRate);
    const long long width = static_cast<long long>(REF_PULSE_S * sampleRate);
    if (period <= 0) return false;
    const long long m = ((k % period) + period) % period;
    return m < width;
}
}  // namespace

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
    state_.decimated = false;  // native mode: no FPGA anti-alias filter, no skew
    return true;
}

bool MockAmplifier::setDecimatedRate(int rate) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Real amplifier accepts any rate value without validation
    // Valid rates: 250, 500, 1000
    state_.decimatedRate = rate;
    state_.decimated = true;  // decimated mode: FPGA filter introduces EEG/physio skew
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

    // Active streaming rate (decimated or native mode).
    int sampleRate = state_.activeRate();

    // Common reference event (emulates the test jig pulsing DIN pin-1 while
    // injecting the same pulse into EEG ch1 and physio ch1). DIN reflects the
    // event immediately; EEG and physio see it delayed in decimated mode so the
    // device's EEG-vs-DIN filter delay and physio-leads-EEG-by-33ms skews appear.
    const long long n = static_cast<long long>(state_.packetCounter);
    // EEG filter delay rounds to the nearest sample (the app applies it as a
    // continuous timestamp offset, so nearest-sample is the closest the mock's
    // sample grid can get). The physio lead floors, matching the device's own
    // whole-sample quantization (32 ms @ 250/500, 33 ms @ 1000).
    const int eegDelaySamples = state_.decimated
        ? (filterDelayMs(sampleRate) * sampleRate + 500) / 1000 : 0;
    const int physioLeadSamples = state_.decimated
        ? PHYSIO_LEAD_MS * sampleRate / 1000 : 0;
    int physioDelaySamples = eegDelaySamples - physioLeadSamples;
    if (physioDelaySamples < 0) physioDelaySamples = 0;

    // DIN is active-low: 0xFFFF idle, pin-1 (bit 0) cleared while the event is on.
    uint16_t din = 0xFFFF;
    if (refActiveAtSample(n, sampleRate)) {
        din &= static_cast<uint16_t>(~0x0001);
    }
    packet.digitalInputs = din;

    // TR byte (255 = no GTEN activity)
    packet.tr = state_.gtenTrainRunning ? 251 : 255;

    // Packet counter and timestamp
    packet.packetCounter = state_.packetCounter;
    packet.timeStamp = getCurrentTimeMicros();

    // Net code
    packet.netCode = static_cast<uint8_t>(state_.netCode);

    // Generate synthetic EEG data
    double dt = 1.0 / sampleRate;
    phase_ += dt;

    float scaleFactor = getScalingFactor(state_.amplifierType);

    // Check if we're in impedance mode (oscillator on, calibration signal configured)
    bool impedanceMode = state_.oscillatorGate &&
                         state_.calibrationSignalFreq > 0 &&
                         state_.calibrationSignalAmplitude > 0;

    // Ideal signal amplitude in microvolts for impedance mode
    // This matches what a 0-ohm electrode would produce
    constexpr double IDEAL_SIGNAL_UV = 100.0;
    constexpr double REFERENCE_RESISTOR_KOHMS = 10.0;

    for (int ch = 0; ch < 256; ++ch) {
        double freq;
        double amplitude;
        double noise = (rand() % 1000 - 500) / 500.0 * 2.0;  // ~2 uV noise

        // Check channel state for impedance measurement
        bool channelDriving = state_.channelDriveSignals[ch] || state_.allDriveSignals;
        bool channel10K = state_.channel10KOhms[ch] || state_.all10KOhms;

        if (impedanceMode) {
            // Use calibration signal frequency
            freq = static_cast<double>(state_.calibrationSignalFreq);

            if (!channelDriving && channel10K) {
                // Channel is in MEASUREMENT mode (drive off, 10K on)
                // Simulate voltage divider: amplitude reduced based on "electrode impedance"
                // Generate a random but stable impedance per channel (5-100 kOhms)
                double simulatedImpedance = 5.0 + (ch * 17 % 95);  // Deterministic pseudo-random

                // Voltage divider: Vout = Vin * R_ref / (R_ref + Z_electrode)
                // amplitude = ideal * R_ref / (R_ref + Z)
                amplitude = IDEAL_SIGNAL_UV * REFERENCE_RESISTOR_KOHMS /
                           (REFERENCE_RESISTOR_KOHMS + simulatedImpedance);
            } else if (channelDriving) {
                // Channel is DRIVING - full calibration signal
                amplitude = IDEAL_SIGNAL_UV;
            } else {
                // Neither driving nor measuring - minimal signal
                amplitude = 5.0;
            }
        } else {
            // Normal EEG mode - generate synthetic brain signals
            freq = 10.0 + (ch % 40);  // Different frequencies per channel
            amplitude = 50.0;  // ~50 uV

            // Add calibration signal if enabled
            if (channelDriving && state_.calibrationSignalFreq > 0) {
                freq = static_cast<double>(state_.calibrationSignalFreq);
                amplitude = state_.calibrationSignalAmplitude > 0 ?
                            static_cast<double>(state_.calibrationSignalAmplitude) : 100.0;
            }
        }

        double value = amplitude * std::sin(2.0 * M_PI * freq * phase_) + noise;

        // Convert to ADC units (reverse of scaling factor)
        packet.eegData[ch] = static_cast<int32_t>(value / scaleFactor);
    }

    // Inject the (filter-delayed) reference pulse into EEG ch1 so it lags the DIN
    // pin-1 edge by the anti-alias filter delay (skip in impedance mode).
    if (!impedanceMode && refActiveAtSample(n - eegDelaySamples, sampleRate)) {
        packet.eegData[0] += static_cast<int32_t>(REF_EEG_PULSE_UV / scaleFactor);
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

    // Inject the reference pulse into physio ch1 (port 1). It uses a smaller delay
    // than EEG (leads it by PHYSIO_LEAD_MS), so the same event lands at an earlier
    // sample index than in EEG — exactly the skew PhysioDelayLine corrects.
    if (!impedanceMode && (state_.physioConnectionStatus & 0x01) &&
        refActiveAtSample(n - physioDelaySamples, sampleRate)) {
        packet.pib1_Data[0] += static_cast<int32_t>(REF_PHYSIO_PULSE_UV / PHYSIO_SCALE_1_8);
    }

    ++state_.packetCounter;
}

} // namespace mock
