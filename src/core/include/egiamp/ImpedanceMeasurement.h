#ifndef EGIAMP_IMPEDANCEMEASUREMENT_H
#define EGIAMP_IMPEDANCEMEASUREMENT_H

#include "AmpServerConnection.h"
#include "AmpServerProtocol.h"
#include "LSLStreamer.h"
#include "SensorLayout.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace egiamp {

// Callback types
using ImpedanceCallback = std::function<void(int channel, float impedanceKOhms)>;
using ImpedanceStatusCallback = std::function<void(const std::string&)>;

// Timing parameters for impedance measurement
// Updated automatically by setSampleRate().
struct ImpedanceTiming {
    double commandTime = 0.03;    // Time for command to travel to amp (seconds)
    double settleTime = 0.6;     // Time before measuring (seconds)
    double filterTime = 0.5;     // Measurement/collection duration (seconds)
    int peakToPeakSampleCount = 100;  // Samples to use for peak-to-peak calculation
};

// Result for a single channel measurement
struct ChannelImpedance {
    int channel;              // 0-based channel index
    float impedanceKOhms;     // Calculated impedance in kilo-ohms
    float amplitude;          // Measured peak-to-peak amplitude
    bool valid;               // Whether measurement was successful
};

class ImpedanceMeasurement {
public:
    ImpedanceMeasurement(AmpServerConnection& connection, int amplifierId);
    ~ImpedanceMeasurement();

    ImpedanceMeasurement(const ImpedanceMeasurement&) = delete;
    ImpedanceMeasurement& operator=(const ImpedanceMeasurement&) = delete;

    // Configuration
    void setTiming(const ImpedanceTiming& timing) { timing_ = timing; }
    void setSampleRate(int rate);
    void setChannelCount(int count) { channelCount_ = count; }
    void setScalingFactor(float factor) { scalingFactor_ = factor; }
    void setNetSize(int netSize);

    // Override the ideal signal for all channels (disables per-channel calibration)
    void setIdealSignal(float idealSignal) { idealSignal_ = idealSignal; }

    // Callbacks
    void setStatusCallback(ImpedanceStatusCallback cb) { statusCallback_ = std::move(cb); }
    void setImpedanceCallback(ImpedanceCallback cb) { impedanceCallback_ = std::move(cb); }

    // Set up amplifier in impedance measurement state (initial state)
    bool setupImpedanceState();

    // Reset amplifier to default acquisition state
    bool resetToDefaultState();

    // Start continuous impedance scanning (runs in background thread)
    // Cycles through all channels repeatedly until stop() is called
    bool startContinuousScan(LSLStreamer& impedanceStreamer);

    // Stop scanning
    void stop();

    // Check if currently scanning
    bool isScanning() const { return scanning_; }

    // Feed a sample into the measurement buffer (called from data reader thread)
    void feedSample(const PacketFormat2& packet);

    // Measure a single channel (blocking) - for testing/debugging
    ChannelImpedance measureChannel(int channel);

    // Measure reference channel
    ChannelImpedance measureReference();

    // Measure COM channel (NA400/NA410 only)
    ChannelImpedance measureCOM();

    // Measure all channels in a tiling set simultaneously
    std::vector<ChannelImpedance> measureTilingSet(const TilingSet& ts);

private:
    void emitStatus(const std::string& message);
    bool sendCommand(const std::string& cmd, int channel, const std::string& value);

    // Turn measurement on/off for a channel
    // When on=false: drive signal OFF, 10K resistor ON (measuring)
    // When on=true: drive signal ON, 10K resistor OFF (driving)
    void setChannelDriving(int channel, bool driving);
    void setReferenceDriving(bool driving);
    void setCOMDriving(bool driving);

    // Collect a buffer of raw packets for the measurement duration
    std::deque<PacketFormat2> collectSampleBuffer();

    // Extract samples for a single channel from a packet buffer
    std::vector<float> extractChannelSamples(const std::deque<PacketFormat2>& packets, int channel);

    // Legacy single-channel collect (used by measureChannel)
    std::vector<float> collectSamples(int channel, int sampleCount);

    // Calculate peak-to-peak amplitude from samples
    float calculatePeakToPeak(const std::vector<float>& samples);

    // Calibrate per-channel ideal signals by measuring in the all-driving state.
    // Must be called after setupImpedanceState() and with data flowing.
    bool calibrate();

    // Calculate impedance from amplitude (uses per-channel calibrated ideal if available)
    float calculateImpedance(float amplitude, int channel = -1);

    // Scanning thread function
    void scanThread(LSLStreamer& impedanceStreamer);

    AmpServerConnection& connection_;
    int amplifierId_;
    ImpedanceTiming timing_;
    int sampleRate_ = 1000;
    int channelCount_ = 256;
    float scalingFactor_ = 0.0f;
    float idealSignal_ = 0.0f;  // 0 = use calibrated or DEFAULT_IDEAL_SIGNAL
    float meanIdealSignal_ = 0.0f;  // Mean across all normal channels from calibration

    // Per-channel calibrated ideal signals (measured in all-driving state).
    std::vector<float> calibratedIdealSignals_;

    // Net size and sensor layout for tiling-based scanning
    int netSize_ = 0;
    const SensorLayout* layout_ = nullptr;

    // Sample buffer for collecting measurement data
    std::mutex sampleMutex_;
    std::deque<PacketFormat2> sampleBuffer_;
    std::atomic<bool> collectingSamples_{false};

    // Scanning state
    std::atomic<bool> scanning_{false};
    std::atomic<bool> stopFlag_{false};
    std::unique_ptr<std::thread> scanThread_;
    std::unique_ptr<std::thread> publishThread_;

    // Current impedance values for all channels (updated as measured)
    // Initialized to MAX_IMPEDANCE to indicate "not yet measured"
    std::vector<float> currentImpedances_;
    std::mutex impedancesMutex_;

    // Publishing thread function (pushes at 1 Hz)
    void publishThread(LSLStreamer& impedanceStreamer);

    ImpedanceStatusCallback statusCallback_;
    ImpedanceCallback impedanceCallback_;
};

} // namespace egiamp

#endif // EGIAMP_IMPEDANCEMEASUREMENT_H
