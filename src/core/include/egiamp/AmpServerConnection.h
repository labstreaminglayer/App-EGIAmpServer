#ifndef EGIAMP_AMPSERVERCONNECTION_H
#define EGIAMP_AMPSERVERCONNECTION_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include <asio.hpp>

namespace egiamp {

using socket_stream = asio::ip::tcp::iostream;

class AmpServerConnection {
public:
    AmpServerConnection();
    ~AmpServerConnection();

    AmpServerConnection(const AmpServerConnection&) = delete;
    AmpServerConnection& operator=(const AmpServerConnection&) = delete;

    bool connect(const std::string& address, uint16_t cmdPort,
                 uint16_t notifyPort, uint16_t dataPort);
    void disconnect();
    bool isConnected() const;

    // Reconnect just the data stream (used to reset stream position after detection)
    bool reconnectDataStream();

    std::string sendCommand(const std::string& command, int ampId,
                            int channel = 0, const std::string& value = "0");
    void sendDatastreamCommand(const std::string& command, int ampId,
                               int channel = 0, const std::string& value = "0");

    socket_stream& commandStream() { return commandStream_; }
    socket_stream& notificationStream() { return notificationStream_; }
    socket_stream& dataStream() { return dataStream_; }

    template<typename Rep, typename Period>
    void setDataStreamTimeout(std::chrono::duration<Rep, Period> timeout) {
        dataStream_.expires_after(timeout);
    }

    template<typename Rep, typename Period>
    void setNotificationStreamTimeout(std::chrono::duration<Rep, Period> timeout) {
        notificationStream_.expires_after(timeout);
    }

private:
    socket_stream commandStream_;
    socket_stream notificationStream_;
    socket_stream dataStream_;
    std::atomic<bool> connected_{false};
    std::string serverAddress_;
    uint16_t dataPort_{0};
};

} // namespace egiamp

#endif // EGIAMP_AMPSERVERCONNECTION_H
