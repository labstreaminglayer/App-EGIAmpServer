#include "egiamp/ImpedanceMeasurement.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace egiamp {

namespace {

// Maximum impedance value to report (kOhms) - values above this are clipped
constexpr float MAX_IMPEDANCE_KOHMS = 1000.0f;

// Default ideal signal (expected amplitude with 0 impedance)
// Based on IDEAL_SIGNAL constant in Net Station (5000.0 with gains ~1.0)
constexpr float DEFAULT_IDEAL_SIGNAL = 5000.0f;

// Reference resistor value in the measurement circuit (kOhms)
constexpr float REFERENCE_RESISTOR_KOHMS = 10.0f;

// NA400 has 2K internal resistors that need to be subtracted
constexpr float NA400_ADJUSTMENT_KOHMS = 2.0f;

} // anonymous namespace

ImpedanceMeasurement::ImpedanceMeasurement(AmpServerConnection& connection, int amplifierId)
    : connection_(connection)
    , amplifierId_(amplifierId)
{
}

ImpedanceMeasurement::~ImpedanceMeasurement() {
    stop();
}

void ImpedanceMeasurement::emitStatus(const std::string& message) {
    if (statusCallback_) {
        statusCallback_(message);
    }
}

bool ImpedanceMeasurement::sendCommand(const std::string& cmd, int channel, const std::string& value) {
    try {
        std::string response = connection_.sendCommand(cmd, amplifierId_, channel, value);
        return response.find("(status complete)") != std::string::npos;
    } catch (...) {
        return false;
    }
}

bool ImpedanceMeasurement::setupImpedanceState() {
    emitStatus("Setting up impedance measurement state...\n");

    // Set the correct initial impedance state based on Net Station's impedanceState()
    // All channels are DRIVING (drive signals ON, 10K resistors OFF)
    // The oscillator generates a 20Hz sine wave calibration signal

    const std::pair<std::string, std::string> commands[] = {
        // Turn OFF all 10K resistors (channels will be driving)
        {"cmd_TurnAll10KOhms", "0"},

        // Turn ON all drive signals (all channels driving the calibration signal)
        {"cmd_TurnAllDriveSignals", "1"},

        // Subject ground OFF
        {"cmd_SetSubjectGround", "0"},

        // Current source OFF
        {"cmd_SetCurrentSource", "0"},

        // Calibration signal frequency: 20 Hz
        {"cmd_SetCalibrationSignalFreq", "20"},

        // Wave shape: sine wave (0 = sine)
        {"cmd_SetWaveShape", "0"},

        // Buffered reference OFF
        {"cmd_SetBufferedReference", "0"},

        // Oscillator gate ON (enables the calibration oscillator)
        {"cmd_SetOscillatorGate", "1"},

        // Reference 10K OFF
        {"cmd_SetReference10KOhms", "0"},

        // Reference drive signal OFF
        {"cmd_SetReferenceDriveSignal", "0"},

        // Driven leg (common) OFF
        {"cmd_SetDrivenCommon", "0"},

        // Calibration amplitude: maximum (4095 = 12 bits all set)
        {"cmd_SetCalibrationSignalAmplitude", "4095"},
    };

    for (const auto& [cmd, value] : commands) {
        if (!sendCommand(cmd, 0, value)) {
            emitStatus("Failed to send command: " + cmd + "\n");
            return false;
        }
    }

    emitStatus("Impedance state configured.\n");
    return true;
}

bool ImpedanceMeasurement::resetToDefaultState() {
    emitStatus("Resetting to default acquisition state...\n");
    return sendCommand("cmd_DefaultAcquisitionState", 0, "0");
}

void ImpedanceMeasurement::setChannelDriving(int channel, bool driving) {
    // When driving=true: drive signal ON, 10K resistor OFF (normal/driving state)
    // When driving=false: drive signal OFF, 10K resistor ON (measurement state)
    sendCommand("cmd_TurnChannelDriveSignals", channel, driving ? "1" : "0");
    sendCommand("cmd_TurnChannel10KOhms", channel, driving ? "0" : "1");
}

void ImpedanceMeasurement::setReferenceDriving(bool driving) {
    // Reference channel has different commands
    // BufferedReference should match the 10K state
    sendCommand("cmd_SetBufferedReference", 0, driving ? "0" : "1");
    sendCommand("cmd_SetReferenceDriveSignal", 0, driving ? "0" : "1");
    sendCommand("cmd_SetReference10KOhms", 0, driving ? "0" : "1");
}

void ImpedanceMeasurement::setCOMDriving(bool driving) {
    // COM channel (NA400/NA410 only)
    sendCommand("cmd_SetCOMDriveSignal", 0, driving ? "1" : "0");
    sendCommand("cmd_SetCOM10KOhms", 0, driving ? "0" : "1");
}

void ImpedanceMeasurement::feedSample(const PacketFormat2& packet) {
    if (!collectingSamples_) {
        return;
    }

    std::lock_guard<std::mutex> lock(sampleMutex_);
    sampleBuffer_.push_back(packet);

    // Keep buffer from growing too large
    int maxSamples = sampleRate_ * 3;  // 3 seconds max
    while (sampleBuffer_.size() > static_cast<size_t>(maxSamples)) {
        sampleBuffer_.pop_front();
    }
}

std::vector<float> ImpedanceMeasurement::collectSamples(int channel, int sampleCount) {
    // Clear existing buffer and start collecting
    {
        std::lock_guard<std::mutex> lock(sampleMutex_);
        sampleBuffer_.clear();
        currentMeasuringChannel_ = channel;
    }
    collectingSamples_ = true;

    // Wait for samples to accumulate
    // Total time = command time + settle time + filter time
    double totalWaitSeconds = timing_.commandTime + timing_.settleTime + timing_.filterTime;
    int totalSamplesNeeded = static_cast<int>(totalWaitSeconds * sampleRate_) + timing_.peakToPeakSampleCount;

    auto startTime = std::chrono::steady_clock::now();
    auto timeout = std::chrono::milliseconds(static_cast<int>((totalWaitSeconds + 1.0) * 1000));

    while (!stopFlag_) {
        {
            std::lock_guard<std::mutex> lock(sampleMutex_);
            if (sampleBuffer_.size() >= static_cast<size_t>(totalSamplesNeeded)) {
                break;
            }
        }

        auto elapsed = std::chrono::steady_clock::now() - startTime;
        if (elapsed > timeout) {
            emitStatus("Timeout waiting for samples on channel " + std::to_string(channel) + "\n");
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    collectingSamples_ = false;

    // Extract the last peakToPeakSampleCount samples for the specified channel
    std::vector<float> samples;
    {
        std::lock_guard<std::mutex> lock(sampleMutex_);

        int samplesToSkip = static_cast<int>(sampleBuffer_.size()) - timing_.peakToPeakSampleCount;
        if (samplesToSkip < 0) samplesToSkip = 0;

        int idx = 0;
        for (const auto& packet : sampleBuffer_) {
            if (idx >= samplesToSkip && channel >= 0 && channel < 280) {
                // Convert to microvolts
                float value = static_cast<float>(packet.eegData[channel]) * scalingFactor_;
                samples.push_back(value);
            }
            idx++;
        }

        sampleBuffer_.clear();
        currentMeasuringChannel_ = -1;
    }

    return samples;
}

float ImpedanceMeasurement::calculatePeakToPeak(const std::vector<float>& samples) {
    if (samples.empty()) {
        return 0.0f;
    }

    auto [minIt, maxIt] = std::minmax_element(samples.begin(), samples.end());
    return *maxIt - *minIt;
}

float ImpedanceMeasurement::calculateImpedance(float amplitude) {
    if (amplitude <= 0.0f) {
        return MAX_IMPEDANCE_KOHMS;
    }

    // Use default ideal signal if not set
    float ideal = idealSignal_;
    if (ideal <= 0.0f) {
        ideal = DEFAULT_IDEAL_SIGNAL;
    }

    // Impedance formula from Net Station:
    // impedance = (idealSignal - amplitude) / (amplitude / 10.0)
    // Where 10.0 is the reference resistor value in kOhms
    float impedance = (ideal - amplitude) / (amplitude / REFERENCE_RESISTOR_KOHMS);

    // Clip to valid range
    if (impedance < 0.0f) {
        impedance = 0.0f;
    }
    if (impedance > MAX_IMPEDANCE_KOHMS) {
        impedance = MAX_IMPEDANCE_KOHMS;
    }

    // Apply NA400 adjustment (subtract 2K internal resistors)
    // TODO: Make this conditional on amplifier type
    impedance -= NA400_ADJUSTMENT_KOHMS;
    if (impedance < 0.0f) {
        impedance = 0.0f;
    }

    return impedance;
}

ChannelImpedance ImpedanceMeasurement::measureChannel(int channel) {
    ChannelImpedance result;
    result.channel = channel;
    result.valid = false;
    result.impedanceKOhms = MAX_IMPEDANCE_KOHMS;
    result.amplitude = 0.0f;

    if (channel < 0 || channel >= channelCount_) {
        return result;
    }

    // Put channel in measurement mode (drive OFF, 10K ON)
    setChannelDriving(channel, false);

    // Collect samples
    std::vector<float> samples = collectSamples(channel, timing_.peakToPeakSampleCount);

    // Reset channel to driving mode
    setChannelDriving(channel, true);

    if (samples.size() < static_cast<size_t>(timing_.peakToPeakSampleCount / 2)) {
        emitStatus("Insufficient samples for channel " + std::to_string(channel) + "\n");
        return result;
    }

    // Calculate amplitude and impedance
    result.amplitude = calculatePeakToPeak(samples);
    result.impedanceKOhms = calculateImpedance(result.amplitude);
    result.valid = true;

    // Estimate ideal signal from first valid measurement if not set
    if (!idealSignalEstimated_ && result.amplitude > 0 && idealSignal_ <= 0) {
        // For a typical good electrode (~5 kOhm), the amplitude should be close to ideal
        // We'll use the first measurement to establish a baseline
        // This is a rough estimate - proper calibration uses gains measurement
        idealSignal_ = result.amplitude * 1.5f;  // Assume ~5kOhm gives 2/3 of ideal
        idealSignalEstimated_ = true;
        emitStatus("Estimated ideal signal: " + std::to_string(idealSignal_) + " uV p-p\n");
    }

    return result;
}

ChannelImpedance ImpedanceMeasurement::measureReference() {
    ChannelImpedance result;
    result.channel = -1;  // Special indicator for reference
    result.valid = false;
    result.impedanceKOhms = MAX_IMPEDANCE_KOHMS;
    result.amplitude = 0.0f;

    // Put reference in measurement mode
    setReferenceDriving(false);

    // Collect samples from reference monitor
    // Note: Reference uses a different data field, but we'll use the last EEG channel
    // as a proxy for now. In a full implementation, we'd read packet.refMonitor
    std::vector<float> samples = collectSamples(channelCount_ - 1, timing_.peakToPeakSampleCount);

    // Reset reference to driving mode
    setReferenceDriving(true);

    if (samples.size() < static_cast<size_t>(timing_.peakToPeakSampleCount / 2)) {
        return result;
    }

    result.amplitude = calculatePeakToPeak(samples);
    result.impedanceKOhms = calculateImpedance(result.amplitude);
    result.valid = true;

    return result;
}

ChannelImpedance ImpedanceMeasurement::measureCOM() {
    ChannelImpedance result;
    result.channel = -2;  // Special indicator for COM
    result.valid = false;
    result.impedanceKOhms = MAX_IMPEDANCE_KOHMS;
    result.amplitude = 0.0f;

    // Put COM in measurement mode
    setCOMDriving(false);

    // For COM, we need to collect from a specific source
    // Using a proxy for now
    std::vector<float> samples = collectSamples(0, timing_.peakToPeakSampleCount);

    // Reset COM to driving mode
    setCOMDriving(true);

    if (samples.size() < static_cast<size_t>(timing_.peakToPeakSampleCount / 2)) {
        return result;
    }

    result.amplitude = calculatePeakToPeak(samples);
    result.impedanceKOhms = calculateImpedance(result.amplitude);
    result.valid = true;

    return result;
}

bool ImpedanceMeasurement::startContinuousScan(LSLStreamer& impedanceStreamer) {
    if (scanning_) {
        return false;
    }

    // Initialize current impedances to max value (not yet measured)
    {
        std::lock_guard<std::mutex> lock(impedancesMutex_);
        currentImpedances_.assign(channelCount_, MAX_IMPEDANCE_KOHMS);
    }

    stopFlag_ = false;
    scanning_ = true;

    // Start publish thread (pushes values at 1 Hz)
    publishThread_ = std::make_unique<std::thread>([this, &impedanceStreamer]() {
        publishThread(impedanceStreamer);
    });

    // Start scan thread (measures channels)
    scanThread_ = std::make_unique<std::thread>([this, &impedanceStreamer]() {
        scanThread(impedanceStreamer);
    });

    return true;
}

void ImpedanceMeasurement::stop() {
    stopFlag_ = true;
    collectingSamples_ = false;

    if (scanThread_ && scanThread_->joinable()) {
        scanThread_->join();
        scanThread_.reset();
    }

    if (publishThread_ && publishThread_->joinable()) {
        publishThread_->join();
        publishThread_.reset();
    }

    scanning_ = false;
}

void ImpedanceMeasurement::publishThread(LSLStreamer& impedanceStreamer) {
    emitStatus("Impedance publish thread started (1 Hz).\n");

    while (!stopFlag_) {
        // Push current impedance values to LSL
        if (impedanceStreamer.hasOutlet()) {
            std::vector<float> values;
            {
                std::lock_guard<std::mutex> lock(impedancesMutex_);
                values = currentImpedances_;
            }

            if (!values.empty()) {
                impedanceStreamer.pushSample(values);
            }
        }

        // Wait 1 second before next push
        for (int i = 0; i < 10 && !stopFlag_; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    emitStatus("Impedance publish thread stopped.\n");
}

void ImpedanceMeasurement::scanThread(LSLStreamer& /* impedanceStreamer */) {
    emitStatus("Starting continuous impedance scan...\n");
    emitStatus("Measuring " + std::to_string(channelCount_) + " channels\n");

    int scanCount = 0;

    while (!stopFlag_) {
        scanCount++;
        emitStatus("Impedance scan #" + std::to_string(scanCount) + " starting...\n");

        // Measure each channel one at a time
        for (int ch = 0; ch < channelCount_ && !stopFlag_; ch++) {
            ChannelImpedance result = measureChannel(ch);

            if (result.valid) {
                // Update the current impedance value for this channel
                {
                    std::lock_guard<std::mutex> lock(impedancesMutex_);
                    if (ch < static_cast<int>(currentImpedances_.size())) {
                        currentImpedances_[ch] = result.impedanceKOhms;
                    }
                }

                // Call per-channel callback if set
                if (impedanceCallback_) {
                    impedanceCallback_(ch, result.impedanceKOhms);
                }
            }

            // Progress update every 10 channels
            if ((ch + 1) % 10 == 0 || ch == channelCount_ - 1) {
                emitStatus("  Measured " + std::to_string(ch + 1) + "/" +
                           std::to_string(channelCount_) + " channels\n");
            }
        }

        // Measure reference
        if (!stopFlag_) {
            ChannelImpedance refResult = measureReference();
            if (refResult.valid) {
                emitStatus("  Reference: " + std::to_string(refResult.impedanceKOhms) + " kOhms\n");
            }
        }

        emitStatus("Scan #" + std::to_string(scanCount) + " complete.\n");
    }

    emitStatus("Impedance scanning stopped.\n");
}

} // namespace egiamp
