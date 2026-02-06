#ifndef EGIAMP_IMPEDANCEMEASUREMENT_H
#define EGIAMP_IMPEDANCEMEASUREMENT_H

#include "AmpServerConnection.h"
#include "AmpServerProtocol.h"
#include "LSLStreamer.h"

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

// Timing parameters for impedance measurement (from Net Station Impedances.plist)
struct ImpedanceTiming {
    double commandTime = 0.03;    // Time for command to travel to amp (seconds)
    double settleTime = 0.0;      // Time before applying filter (seconds) - NS uses 0
    double filterTime = 1.0;      // Filter/measurement duration (seconds)
    int peakToPeakSampleCount = 51;  // Samples to use for peak-to-peak calculation
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
    void setSampleRate(int rate) { sampleRate_ = rate; }
    void setChannelCount(int count) { channelCount_ = count; }
    void setScalingFactor(float factor) { scalingFactor_ = factor; }

    // Set the ideal signal (expected amplitude with 0 impedance)
    // If not set, will be estimated from first measurement
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

private:
    void emitStatus(const std::string& message);
    bool sendCommand(const std::string& cmd, int channel, const std::string& value);

    // Turn measurement on/off for a channel
    // When on=false: drive signal OFF, 10K resistor ON (measuring)
    // When on=true: drive signal ON, 10K resistor OFF (driving)
    void setChannelDriving(int channel, bool driving);
    void setReferenceDriving(bool driving);
    void setCOMDriving(bool driving);

    // Collect samples for the measurement duration
    std::vector<float> collectSamples(int channel, int sampleCount);

    // Calculate peak-to-peak amplitude from samples
    float calculatePeakToPeak(const std::vector<float>& samples);

    // Calculate impedance from amplitude
    float calculateImpedance(float amplitude);

    // Scanning thread function
    void scanThread(LSLStreamer& impedanceStreamer);

    AmpServerConnection& connection_;
    int amplifierId_;
    ImpedanceTiming timing_;
    int sampleRate_ = 1000;
    int channelCount_ = 256;
    float scalingFactor_ = 0.0f;
    float idealSignal_ = 0.0f;  // Will be estimated if not set
    bool idealSignalEstimated_ = false;

    // Sample buffer for collecting measurement data
    std::mutex sampleMutex_;
    std::deque<PacketFormat2> sampleBuffer_;
    int currentMeasuringChannel_ = -1;  // Channel currently being measured (-1 = none)
    bool collectingSamples_ = false;

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
