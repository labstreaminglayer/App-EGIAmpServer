#ifndef EGIAMP_EGIAMPCLIENT_H
#define EGIAMP_EGIAMPCLIENT_H

#include "AmpServerConfig.h"
#include "AmpServerConnection.h"
#include "AmpServerProtocol.h"
#include "LSLStreamer.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace egiamp {

using StatusCallback = std::function<void(const std::string&)>;
using ErrorCallback = std::function<void(const std::string&)>;
using ChannelCountCallback = std::function<void(int)>;

struct AmplifierDetails {
    std::string serialNumber;
    std::string firmwareVersion;
    AmplifierType amplifierType = AmplifierType::Unknown;
    PacketType packetType = PacketType::Format1;
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

    const AmplifierDetails& amplifierDetails() const { return details_; }

    void setStatusCallback(StatusCallback cb) { statusCallback_ = std::move(cb); }
    void setErrorCallback(ErrorCallback cb) { errorCallback_ = std::move(cb); }
    void setChannelCountCallback(ChannelCountCallback cb) { channelCountCallback_ = std::move(cb); }

    void run();

private:
    void emitStatus(const std::string& message);
    void emitError(const std::string& message);
    void emitChannelCount(int count);
    static bool commandCompleted(const std::string& response);

    bool queryAmplifierDetails();
    bool initAmplifier();
    void haltAmplifier();

    void readPacketFormat1();
    void readPacketFormat2();
    void processNotifications();
    bool cmd_ImpedanceAcquisitionState();

    AmpServerConfig config_;
    AmpServerConnection connection_;
    LSLStreamer streamer_;
    AmplifierDetails details_;

    std::atomic<bool> stopFlag_{false};
    std::unique_ptr<std::thread> readerThread_;
    std::unique_ptr<std::thread> notificationThread_;

    uint64_t lastPacketCounter_ = 0;
    uint64_t lastTimeStamp_ = 0;
    uint64_t lastPacketCounterWithTimeStamp_ = 0;

    StatusCallback statusCallback_;
    ErrorCallback errorCallback_;
    ChannelCountCallback channelCountCallback_;
};

} // namespace egiamp

#endif // EGIAMP_EGIAMPCLIENT_H
