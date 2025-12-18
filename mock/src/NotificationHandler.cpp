#include "NotificationHandler.h"
#include <iostream>
#include <sstream>

namespace mock {

NotificationHandler::NotificationHandler(asio::io_context& io_context, uint16_t port,
                                         std::shared_ptr<MockAmplifier> amplifier)
    : io_context_(io_context)
    , acceptor_(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
    , amplifier_(amplifier)
{
}

NotificationHandler::~NotificationHandler() {
    stop();
}

void NotificationHandler::start() {
    running_ = true;
    acceptConnections();
    std::cout << "[NotificationHandler] Listening on port " << acceptor_.local_endpoint().port() << std::endl;
}

void NotificationHandler::stop() {
    running_ = false;
    asio::error_code ec;
    acceptor_.close(ec);

    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (auto& client : clients_) {
        client->close(ec);
    }
    clients_.clear();
}

void NotificationHandler::acceptConnections() {
    auto socket = std::make_shared<asio::ip::tcp::socket>(io_context_);
    acceptor_.async_accept(*socket, [this, socket](const asio::error_code& ec) {
        if (!ec && running_) {
            std::cout << "[NotificationHandler] Client connected" << std::endl;
            handleConnection(socket);
        }
        if (running_) {
            acceptConnections();
        }
    });
}

void NotificationHandler::handleConnection(std::shared_ptr<asio::ip::tcp::socket> socket) {
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_.insert(socket);
    }

    // Send initial connection notification
    int clientId = nextClientId_++;
    sendClientConnected(clientId);

    // Read from client (to detect disconnection)
    readFromClient(socket);
}

void NotificationHandler::readFromClient(std::shared_ptr<asio::ip::tcp::socket> socket) {
    auto buffer = std::make_shared<std::array<char, 1024>>();
    socket->async_read_some(asio::buffer(*buffer),
        [this, socket, buffer](const asio::error_code& ec, std::size_t bytes_transferred) {
            if (ec) {
                removeClient(socket);
            } else if (running_) {
                // Handle any commands received on notification port
                std::string data(buffer->data(), bytes_transferred);
                std::cout << "[NotificationHandler] Received: " << data << std::endl;
                readFromClient(socket);
            }
        });
}

void NotificationHandler::removeClient(std::shared_ptr<asio::ip::tcp::socket> socket) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    clients_.erase(socket);
    asio::error_code ec;
    socket->close(ec);
    std::cout << "[NotificationHandler] Client disconnected" << std::endl;
}

void NotificationHandler::sendNotification(const std::string& notification) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    std::string message = notification + "\n";

    for (auto it = clients_.begin(); it != clients_.end(); ) {
        auto& socket = *it;
        asio::error_code ec;
        asio::write(*socket, asio::buffer(message), ec);
        if (ec) {
            it = clients_.erase(it);
        } else {
            ++it;
        }
    }
}

void NotificationHandler::sendClientConnected(int clientId) {
    std::ostringstream oss;
    oss << "(sendCommand_return (status complete) (amp_server_status_info "
        << "(client_connected (client_id " << clientId << "))))";
    sendNotification(oss.str());
}

void NotificationHandler::sendPhysioConnectionStatus(int status) {
    std::ostringstream oss;
    oss << "(notification (physio_connection_status " << status << "))";
    sendNotification(oss.str());
}

void NotificationHandler::sendGTENStatus() {
    const auto& state = amplifier_->state();
    std::ostringstream oss;
    oss << "(notification (ntn_GTENGetStatus "
        << "(Firmware_Version " << state.firmwareVersion << ") "
        << "(Remaining_Time_ms " << state.gtenRemainingTimeMs << ") "
        << "(Sentinal_Alarm " << (state.gtenAlarm ? "On" : "Off") << ") "
        << "(Serial_Number " << state.serialNumber << ") "
        << "(Train_Running " << (state.gtenTrainRunning ? "Yes" : "No") << ") "
        << "(Waveform_Pulsed " << (state.gtenWaveformPulsed ? "Yes" : "No") << ") "
        << "(Waveform_Type " << state.gtenWaveformType << ")))";
    sendNotification(oss.str());
}

void NotificationHandler::sendGTENSetTrainResult(bool success) {
    if (success) {
        sendNotification("(notification (ntn_GTENSetTrainSucceeded))");
    } else {
        sendNotification("(notification (ntn_GTENSetTrainFailed))");
    }
}

void NotificationHandler::sendGTENSetWaveformResult(bool success) {
    if (success) {
        sendNotification("(notification (ntn_GTENSetWaveformSucceeded))");
    } else {
        sendNotification("(notification (ntn_GTENSetWaveformFailed))");
    }
}

void NotificationHandler::sendGTENStartTrainResult(bool success) {
    if (success) {
        sendNotification("(notification (ntn_GTENStartTrainSucceeded))");
    } else {
        sendNotification("(notification (ntn_GTENStartTrainFailed))");
    }
}

void NotificationHandler::sendGTENAbortTrainResult(bool success) {
    if (success) {
        sendNotification("(notification (ntn_GTENAbortTrainSucceeded))");
    } else {
        sendNotification("(notification (ntn_GTENAbortTrainFailed))");
    }
}

void NotificationHandler::sendGTENResetAlarmResult(bool success) {
    if (success) {
        sendNotification("(notification (ntn_GTENResetAlarmSucceeded))");
    } else {
        sendNotification("(notification (ntn_GTENResetAlarmFailed))");
    }
}

} // namespace mock
