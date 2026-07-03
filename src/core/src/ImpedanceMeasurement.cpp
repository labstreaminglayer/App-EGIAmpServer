#include "egiamp/ImpedanceMeasurement.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace egiamp {

namespace {

// Maximum impedance value to report (kOhms) - values above this are clipped
constexpr float MAX_IMPEDANCE_KOHMS = 5000.0f;

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

void ImpedanceMeasurement::setNetSize(int netSize) {
    netSize_ = netSize;
    layout_ = SensorLayout::forNetSize(netSize);
}

void ImpedanceMeasurement::setSampleRate(int rate) {
    sampleRate_ = rate;

    // Update timing parameters based on sample rate.
    // Settle time accounts for the signal stabilising after switching
    // drive/measurement state. The total time per tiling set is approximately
    // N_commands * commandTime + settleTime + filterTime.
    switch (rate) {
        case 250:
            timing_.commandTime = 0.03;
            timing_.settleTime = 0.35;
            timing_.filterTime = 0.5;
            timing_.peakToPeakSampleCount = 62;
            break;
        case 500:
            timing_.commandTime = 0.03;
            timing_.settleTime = 0.5;
            timing_.filterTime = 0.5;
            timing_.peakToPeakSampleCount = 87;
            break;
        default:
            // 1000 Hz and above
            timing_.commandTime = 0.03;
            timing_.settleTime = 0.6;
            timing_.filterTime = 0.5;
            timing_.peakToPeakSampleCount = 100;
            break;
    }
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

bool ImpedanceMeasurement::calibrate() {
    emitStatus("Calibrating gain (measuring ideal signals)...\n");

    // In the all-driving state (set by setupImpedanceState), all channels receive
    // the calibration signal with effectively 0 impedance. The measured peak-to-peak
    // amplitude per channel IS the ideal signal for that channel.
    // This is equivalent to Net Station's gainCalState / gains calibration.
    //
    // Note: channels with Z ≈ 0 still show normal calibration amplitude because
    // the drive circuit injects signal directly. Z ≈ 0 is detected later in
    // calculateImpedance() when measurement amplitude drops to near zero.

    std::deque<PacketFormat2> packets = collectSampleBuffer();

    if (packets.empty()) {
        emitStatus("Calibration failed: no samples collected.\n");
        return false;
    }

    calibratedIdealSignals_.resize(channelCount_, 0.0f);
    int calibrated = 0;
    float sumIdeal = 0.0f;

    for (int ch = 0; ch < channelCount_; ch++) {
        std::vector<float> samples = extractChannelSamples(packets, ch);
        if (samples.size() >= static_cast<size_t>(timing_.peakToPeakSampleCount / 2)) {
            float amplitude = calculatePeakToPeak(samples);
            calibratedIdealSignals_[ch] = amplitude;
            if (amplitude > 0.0f) {
                calibrated++;
                sumIdeal += amplitude;
            }
        }
    }

    if (calibrated == 0) {
        emitStatus("Calibration failed: no valid amplitudes measured.\n");
        return false;
    }

    meanIdealSignal_ = sumIdeal / calibrated;

    emitStatus("Calibration complete: " + std::to_string(calibrated) + "/" +
               std::to_string(channelCount_) + " channels.\n");
    emitStatus("  Mean ideal signal: " + std::to_string(static_cast<int>(meanIdealSignal_)) +
               " uV p-p\n");

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
    // Reference channel has different commands than regular EEG channels.
    // From NS turnCurrent:forElectrodeSet: —
    //   BufferedReference and Reference10KOhms use inverted polarity (!on),
    //   but ReferenceDriveSignal uses direct polarity (on), same as EEG channels.
    sendCommand("cmd_SetBufferedReference", 0, driving ? "0" : "1");
    sendCommand("cmd_SetReferenceDriveSignal", 0, driving ? "1" : "0");
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

std::deque<PacketFormat2> ImpedanceMeasurement::collectSampleBuffer() {
    // Clear existing buffer and start collecting
    {
        std::lock_guard<std::mutex> lock(sampleMutex_);
        sampleBuffer_.clear();
    }
    collectingSamples_ = true;

    // Wait for samples to accumulate
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
            emitStatus("Timeout waiting for sample buffer\n");
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    collectingSamples_ = false;

    // Return the last peakToPeakSampleCount packets
    std::lock_guard<std::mutex> lock(sampleMutex_);
    std::deque<PacketFormat2> result;
    int samplesToSkip = static_cast<int>(sampleBuffer_.size()) - timing_.peakToPeakSampleCount;
    if (samplesToSkip < 0) samplesToSkip = 0;

    auto it = sampleBuffer_.begin();
    std::advance(it, samplesToSkip);
    result.assign(it, sampleBuffer_.end());
    sampleBuffer_.clear();

    return result;
}

std::vector<float> ImpedanceMeasurement::extractChannelSamples(
    const std::deque<PacketFormat2>& packets, int channel) {
    std::vector<float> samples;
    samples.reserve(packets.size());
    for (const auto& packet : packets) {
        if (channel >= 0 && channel < 280) {
            samples.push_back(static_cast<float>(packet.eegData[channel]) * scalingFactor_);
        }
    }
    return samples;
}

std::vector<float> ImpedanceMeasurement::collectSamples(int channel, int sampleCount) {
    // Clear existing buffer and start collecting
    {
        std::lock_guard<std::mutex> lock(sampleMutex_);
        sampleBuffer_.clear();
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

float ImpedanceMeasurement::calculateImpedance(float amplitude, int channel) {
    // Determine ideal signal: global override > per-channel calibration > default
    float ideal = idealSignal_;
    if (ideal <= 0.0f) {
        if (channel >= 0 && channel < static_cast<int>(calibratedIdealSignals_.size()) &&
            calibratedIdealSignals_[channel] > 0.0f) {
            ideal = calibratedIdealSignals_[channel];
        } else {
            ideal = (meanIdealSignal_ > 0.0f) ? meanIdealSignal_ : DEFAULT_IDEAL_SIGNAL;
        }
    }

    // Zero amplitude means all samples were identical — electrode is at
    // reference potential (Z ≈ 0).  Matches Net Station behavior.
    if (amplitude <= 0.0f) {
        return 0.0f;
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
    result.impedanceKOhms = calculateImpedance(result.amplitude, channel);
    result.valid = true;

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

    // Collect sample buffer and extract from the dedicated refMonitor field
    std::deque<PacketFormat2> packets = collectSampleBuffer();

    // Reset reference to driving mode
    setReferenceDriving(true);

    std::vector<float> samples;
    samples.reserve(packets.size());
    for (const auto& pkt : packets) {
        samples.push_back(static_cast<float>(pkt.refMonitor) * scalingFactor_);
    }

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

    // Collect sample buffer and extract from the dedicated comMonitor field
    std::deque<PacketFormat2> packets = collectSampleBuffer();

    // Reset COM to driving mode
    setCOMDriving(true);

    std::vector<float> samples;
    samples.reserve(packets.size());
    for (const auto& pkt : packets) {
        samples.push_back(static_cast<float>(pkt.comMonitor) * scalingFactor_);
    }

    if (samples.size() < static_cast<size_t>(timing_.peakToPeakSampleCount / 2)) {
        return result;
    }

    result.amplitude = calculatePeakToPeak(samples);
    result.impedanceKOhms = calculateImpedance(result.amplitude);
    result.valid = true;

    return result;
}

std::vector<ChannelImpedance> ImpedanceMeasurement::measureTilingSet(const TilingSet& ts) {
    if (ts.isReference) {
        // Delegate to existing reference measurement
        std::vector<ChannelImpedance> results;
        results.push_back(measureReference());
        return results;
    }

    std::vector<ChannelImpedance> results;
    results.reserve(ts.channels.size());

    // Switch all channels in the set to measurement mode simultaneously
    for (int ch : ts.channels) {
        if (ch >= 0 && ch < channelCount_) {
            setChannelDriving(ch, false);
        }
    }

    // Collect one shared sample buffer for all channels
    std::deque<PacketFormat2> packets = collectSampleBuffer();

    // Bulk reset: all drive signals ON, all 10K resistors OFF
    sendCommand("cmd_TurnAllDriveSignals", 0, "1");
    sendCommand("cmd_TurnAll10KOhms", 0, "0");

    // Extract per-channel samples and calculate impedance
    for (int ch : ts.channels) {
        ChannelImpedance result;
        result.channel = ch;
        result.valid = false;
        result.impedanceKOhms = MAX_IMPEDANCE_KOHMS;
        result.amplitude = 0.0f;

        if (ch < 0 || ch >= channelCount_) {
            results.push_back(result);
            continue;
        }

        std::vector<float> samples = extractChannelSamples(packets, ch);

        if (samples.size() < static_cast<size_t>(timing_.peakToPeakSampleCount / 2)) {
            results.push_back(result);
            continue;
        }

        result.amplitude = calculatePeakToPeak(samples);
        result.impedanceKOhms = calculateImpedance(result.amplitude, ch);

        // Tiling set scale correction: when measuring a group, not all other
        // channels are driving, which reduces the measured signal.  Apply a
        // correction factor only to low impedances (≤ 150 kΩ) so that bad
        // channels still stand out.  Matches Net Station behavior.
        constexpr float kMaxApplyScaleValue = 150.0f;
        float scale = static_cast<float>(ts.channels.size())
                    / static_cast<float>(channelCount_);
        if (result.impedanceKOhms <= kMaxApplyScaleValue) {
            result.impedanceKOhms -= scale * result.impedanceKOhms;
            if (result.impedanceKOhms < 0.0f) result.impedanceKOhms = 0.0f;
        }

        result.valid = true;
        results.push_back(result);
    }

    return results;
}

bool ImpedanceMeasurement::startContinuousScan(LSLStreamer& impedanceStreamer) {
    if (scanning_) {
        return false;
    }

    // Initialize current impedances to max value (not yet measured).
    // One extra slot at index channelCount_ holds the reference (Cz) impedance,
    // matching the trailing Cz channel added by createImpedanceOutlet().
    {
        std::lock_guard<std::mutex> lock(impedancesMutex_);
        currentImpedances_.assign(channelCount_ + 1, MAX_IMPEDANCE_KOHMS);
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

    // Calibrate gain before starting measurements.
    // All channels are in driving state (from setupImpedanceState), so the
    // measured amplitude is the ideal signal for each channel.
    if (!stopFlag_) {
        calibrate();
    }

    // Use tiling-based scanning if layout is available
    if (layout_ && !layout_->tilingSets().empty()) {
        const auto& sets = layout_->tilingSets();
        emitStatus("Using tiling-based scanning (" + std::to_string(sets.size()) + " tiling sets)\n");

        int scanCount = 0;
        while (!stopFlag_) {
            scanCount++;
            emitStatus("Impedance scan #" + std::to_string(scanCount) + " starting...\n");

            for (size_t setIdx = 0; setIdx < sets.size() && !stopFlag_; setIdx++) {
                const auto& ts = sets[setIdx];
                std::string setDesc = ts.isReference ? "reference" :
                    std::to_string(ts.channels.size()) + " channels";
                emitStatus("  Tiling set " + std::to_string(setIdx + 1) + "/" +
                           std::to_string(sets.size()) + " (" + setDesc + ")\n");

                std::vector<ChannelImpedance> results = measureTilingSet(ts);

                for (const auto& result : results) {
                    if (!result.valid) {
                        continue;
                    }
                    // measureReference() reports channel == -1; store it in the
                    // trailing reference (Cz) slot at index channelCount_.
                    int slot = (result.channel >= 0) ? result.channel : channelCount_;
                    {
                        std::lock_guard<std::mutex> lock(impedancesMutex_);
                        if (slot < static_cast<int>(currentImpedances_.size())) {
                            currentImpedances_[slot] = result.impedanceKOhms;
                        }
                    }
                    if (impedanceCallback_) {
                        impedanceCallback_(slot, result.impedanceKOhms);
                    }
                }
            }

            emitStatus("Scan #" + std::to_string(scanCount) + " complete.\n");
        }
    } else {
        // Fallback: sequential channel-by-channel scanning
        emitStatus("No tiling layout available, using sequential scanning.\n");

        int scanCount = 0;
        while (!stopFlag_) {
            scanCount++;
            emitStatus("Impedance scan #" + std::to_string(scanCount) + " starting...\n");

            for (int ch = 0; ch < channelCount_ && !stopFlag_; ch++) {
                ChannelImpedance result = measureChannel(ch);

                if (result.valid) {
                    {
                        std::lock_guard<std::mutex> lock(impedancesMutex_);
                        if (ch < static_cast<int>(currentImpedances_.size())) {
                            currentImpedances_[ch] = result.impedanceKOhms;
                        }
                    }
                    if (impedanceCallback_) {
                        impedanceCallback_(ch, result.impedanceKOhms);
                    }
                }

                if ((ch + 1) % 10 == 0 || ch == channelCount_ - 1) {
                    emitStatus("  Measured " + std::to_string(ch + 1) + "/" +
                               std::to_string(channelCount_) + " channels\n");
                }
            }

            // Measure reference (Cz) and publish it in the trailing slot
            if (!stopFlag_) {
                ChannelImpedance refResult = measureReference();
                if (refResult.valid) {
                    {
                        std::lock_guard<std::mutex> lock(impedancesMutex_);
                        if (channelCount_ < static_cast<int>(currentImpedances_.size())) {
                            currentImpedances_[channelCount_] = refResult.impedanceKOhms;
                        }
                    }
                    if (impedanceCallback_) {
                        impedanceCallback_(channelCount_, refResult.impedanceKOhms);
                    }
                    emitStatus("  Reference: " + std::to_string(refResult.impedanceKOhms) + " kOhms\n");
                }
            }

            emitStatus("Scan #" + std::to_string(scanCount) + " complete.\n");
        }
    }

    emitStatus("Impedance scanning stopped.\n");
}

} // namespace egiamp
