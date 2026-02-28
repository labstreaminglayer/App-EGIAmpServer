#include "egiamp/AmpServerConnection.h"

namespace egiamp {

AmpServerConnection::AmpServerConnection() = default;

AmpServerConnection::~AmpServerConnection() {
    disconnect();
}

bool AmpServerConnection::connect(const std::string& address, uint16_t cmdPort,
                                  uint16_t notifyPort, uint16_t dataPort) {
    try {
        // Store for potential reconnection
        serverAddress_ = address;
        dataPort_ = dataPort;

        // Connect command stream
        commandStream_.clear();
        commandStream_.exceptions(std::ios::eofbit | std::ios::failbit | std::ios::badbit);
        commandStream_.expires_after(std::chrono::seconds(2));
        commandStream_.connect(address, std::to_string(cmdPort));
        commandStream_.expires_after(std::chrono::hours(8760)); // ~1 year

        // Connect notification stream
        notificationStream_.clear();
        notificationStream_.expires_after(std::chrono::seconds(2));
        notificationStream_.connect(address, std::to_string(notifyPort));

        // Connect data stream
        dataStream_.clear();
        dataStream_.expires_after(std::chrono::seconds(5));
        dataStream_.connect(address, std::to_string(dataPort));

        connected_ = true;
        return true;
    } catch (...) {
        disconnect();
        return false;
    }
}

bool AmpServerConnection::reconnectDataStream() {
    try {
        dataStream_.close();
        dataStream_.clear();
        dataStream_.expires_after(std::chrono::seconds(5));
        dataStream_.connect(serverAddress_, std::to_string(dataPort_));
        return true;
    } catch (...) {
        return false;
    }
}

void AmpServerConnection::disconnect() {
    connected_ = false;
    commandStream_.close();
    notificationStream_.close();
    dataStream_.close();
}

bool AmpServerConnection::isConnected() const {
    return connected_;
}

std::string AmpServerConnection::sendCommand(const std::string& command, int ampId,
                                             int channel, const std::string& value) {
    char response[4096];
    commandStream_ << "(sendCommand " << command << ' ' << ampId << ' '
                   << channel << ' ' << value << ")\n" << std::flush;
    commandStream_.getline(response, sizeof(response));
    return std::string(response);
}

void AmpServerConnection::sendDatastreamCommand(const std::string& command, int ampId,
                                                int channel, const std::string& value) {
    dataStream_ << "(sendCommand " << command << ' ' << ampId << ' '
                << channel << ' ' << value << ")\n" << std::flush;
}

} // namespace egiamp
