#ifndef MOCK_NOTIFICATIONHANDLER_H
#define MOCK_NOTIFICATIONHANDLER_H

#include "MockAmplifier.h"
#include <asio.hpp>
#include <memory>
#include <mutex>
#include <set>
#include <string>

namespace mock {

class NotificationHandler : public std::enable_shared_from_this<NotificationHandler> {
public:
    NotificationHandler(asio::io_context& io_context, uint16_t port,
                        std::shared_ptr<MockAmplifier> amplifier);
    ~NotificationHandler();

    void start();
    void stop();

    // Send notification to all connected clients
    void sendNotification(const std::string& notification);

    // Specific notification types
    void sendClientConnected(int clientId);
    void sendPhysioConnectionStatus(int status);
    void sendAmpStarted();
    void sendAmpStopped();
    void sendAmpPowerOn();
    void sendAmpPowerOff();
    void sendGTENStatus();
    void sendGTENSetTrainResult(bool success);
    void sendGTENSetWaveformResult(bool success);
    void sendGTENStartTrainResult(bool success);
    void sendGTENAbortTrainResult(bool success);
    void sendGTENResetAlarmResult(bool success);

private:
    void acceptConnections();
    void handleConnection(std::shared_ptr<asio::ip::tcp::socket> socket);
    void readFromClient(std::shared_ptr<asio::ip::tcp::socket> socket);
    void removeClient(std::shared_ptr<asio::ip::tcp::socket> socket);

    asio::io_context& io_context_;
    asio::ip::tcp::acceptor acceptor_;
    std::shared_ptr<MockAmplifier> amplifier_;
    std::atomic<bool> running_{false};

    std::mutex clientsMutex_;
    std::set<std::shared_ptr<asio::ip::tcp::socket>> clients_;
    int nextClientId_ = 1;
    int notificationSeqId_ = 0;  // Sequence ID for notifications
};

} // namespace mock

#endif // MOCK_NOTIFICATIONHANDLER_H
