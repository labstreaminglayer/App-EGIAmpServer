#ifndef EGIAMP_EGIAMPCLIENT_H
#define EGIAMP_EGIAMPCLIENT_H

#include "AmpServerConfig.h"
#include "AmpServerConnection.h"
#include "AmpServerProtocol.h"
#include "ImpedanceMeasurement.h"
#include "LSLStreamer.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace egiamp {

using StatusCallback = std::function<void(const std::string&)>;
using ErrorCallback = std::function<void(const std::string&)>;
using ChannelCountCallback = std::function<void(int)>;
using SensorCallback = std::function<void(NetCode)>;

struct AmplifierDetails {
    std::string serialNumber;
    std::string firmwareVersion;
    AmplifierType amplifierType = AmplifierType::Unknown;
    PacketType packetType = PacketType::Format1;
    NetCode netCode = NetCode::Unknown;
    int channelCount = 0;
    float scalingFactor = 0.0f;
};

class EGIAmpClient {
public:
    EGIAmpClient();
    ~EGIAmpClient();

    EGIAmpClient(const EGIAmpClient&) = delete;
    EGIAmpClient& operator=(const EGIAmpClient&) = delete;

    void setConfig(const AmpServerConfig& config);
    const AmpServerConfig& config() const { return config_; }

    bool connect();
    void disconnect();
    bool isConnected() const;
    bool isStreaming() const;

    bool startStreaming();
    void stopStreaming();

    // Dynamic impedance mode control (for GUI toggle while streaming)
    // Returns false if not currently streaming or if operation fails
    bool startImpedanceMode();
    bool stopImpedanceMode();
    bool isImpedanceModeActive() const;

    // Shutdown the Amp Server process (cmd_Exit)
    // Warning: This terminates the Amp Server and will disconnect all clients
    bool shutdownAmpServer();

    // Query amp state - returns true if amp is already running, updates detectedSampleRate
    bool queryAmpState();
    int detectedSampleRate() const { return detectedSampleRate_; }
    bool ampWasRunning() const { return ampWasRunning_; }
    NetCode detectedNetCode() const { return detectedNetCode_; }
    int physioConnectionStatus() const { return physioConnectionStatus_; }

    const AmplifierDetails& amplifierDetails() const { return details_; }

    void setStatusCallback(StatusCallback cb) { statusCallback_ = std::move(cb); }
    void setErrorCallback(ErrorCallback cb) { errorCallback_ = std::move(cb); }
    void setChannelCountCallback(ChannelCountCallback cb) { channelCountCallback_ = std::move(cb); }
    void setSensorCallback(SensorCallback cb) { sensorCallback_ = std::move(cb); }

    void run();

private:
    void emitStatus(const std::string& message);
    void emitError(const std::string& message);
    void emitChannelCount(int count);
    void emitSensor(NetCode code);

    bool queryAmplifierDetails();
    bool isAmplifierStreaming();
    int detectSampleRate();  // Returns detected sample rate, or 0 if detection failed
    bool initAmplifier();
    void haltAmplifier();

    void readPacketFormat1();
    void readPacketFormat2();
    void processNotifications();

    bool attemptRecovery(bool reinitialize);
    void reQueryPhysioStatus();

    bool cmd_ImpedanceAcquisitionState();
    static bool commandCompleted(const std::string& response);

    AmpServerConfig config_;
    AmpServerConnection connection_;
    LSLStreamer streamer_;
    LSLStreamer dinStreamer_;
    LSLStreamer impedanceStreamer_;
    AmplifierDetails details_;
    std::unique_ptr<ImpedanceMeasurement> impedanceMeasurement_;

    std::atomic<bool> stopFlag_{false};
    std::atomic<bool> sampleRateChangeDetected_{false};  // Set when ntn_AmpStarted received
    std::atomic<bool> impedanceModeActive_{false};  // Dynamic impedance mode state
    std::atomic<bool> streamLost_{false};       // Data stream died, waiting for recovery
    std::atomic<bool> ampRestarted_{false};     // Recovery complete, reader thread should resume
    std::atomic<int> recoveryAttempts_{0};      // Number of recovery attempts since last success
    std::mutex recoveryMutex_;
    std::condition_variable recoveryCv_;
    bool weInitializedAmp_{false};  // Track if we initialized the amp (vs. it was already running)
    bool ampWasRunning_{false};     // Was the amp running when we queried state?
    int detectedSampleRate_{0};     // Sample rate detected from running amp
    NetCode detectedNetCode_{NetCode::Unknown};  // Sensor net code detected from running amp
    int physioConnectionStatus_{0};  // 0=none, 1=port1(16ch), 2=port2(16ch), 3=both(32ch)
    std::unique_ptr<std::thread> readerThread_;
    std::unique_ptr<std::thread> notificationThread_;

    uint64_t lastPacketCounter_ = 0;
    uint64_t lastTimeStamp_ = 0;
    uint64_t lastPacketCounterWithTimeStamp_ = 0;
    uint16_t lastDINValue_ = 0;

    StatusCallback statusCallback_;
    ErrorCallback errorCallback_;
    ChannelCountCallback channelCountCallback_;
    SensorCallback sensorCallback_;
};

} // namespace egiamp

#endif // EGIAMP_EGIAMPCLIENT_H
