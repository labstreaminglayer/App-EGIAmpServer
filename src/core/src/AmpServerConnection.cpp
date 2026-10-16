#include "egiamp/AmpServerConnection.h"

#include <stdexcept>

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
    // Build the whole command first and write it in a single insertion: the
    // NA500's AmpServer parses each TCP segment as a complete message, so a
    // command split across segments (which multiple << insertions can cause)
    // desyncs its request/response handling.
    const std::string message = "(sendCommand " + command + ' ' + std::to_string(ampId) +
                                ' ' + std::to_string(channel) + ' ' + value + ")\n";
    // Bound the response wait: a half-dead AmpServer (observed on the NA500
    // after firmware crashes) accepts connections but never responds, and an
    // unbounded getline would block the caller forever.
    try {
        commandStream_.expires_after(std::chrono::seconds(10));
        commandStream_ << message << std::flush;
        commandStream_.getline(response, sizeof(response));
        commandStream_.expires_after(std::chrono::hours(8760));
    } catch (...) {
        commandStream_.clear();
        commandStream_.expires_after(std::chrono::hours(8760));
        throw std::runtime_error("No response from AmpServer for " + command +
                                 " within 10 s (server unresponsive?)");
    }

    // Resynchronize: the NA500's AmpServer occasionally delivers a stale line
    // (e.g. left over from a previous client session) ahead of the actual
    // response, which would desync every subsequent request/response pair.
    // Skip lines that are not command responses, bounded so a misbehaving
    // server cannot stall us for long.
    for (int skipped = 0;
         skipped < 5 && std::string(response).find("sendCommand_return") == std::string::npos;
         ++skipped) {
        try {
            commandStream_.expires_after(std::chrono::seconds(2));
            commandStream_.getline(response, sizeof(response));
            commandStream_.expires_after(std::chrono::hours(8760));
        } catch (...) {
            // Timed out waiting for a proper response line — give up on the
            // resync and return what we have; callers treat it as a soft error.
            commandStream_.clear();
            commandStream_.expires_after(std::chrono::hours(8760));
            break;
        }
    }
    return std::string(response);
}

void AmpServerConnection::sendDatastreamCommand(const std::string& command, int ampId,
                                                int channel, const std::string& value) {
    // Single insertion for the same fragmentation reason as sendCommand().
    const std::string message = "(sendCommand " + command + ' ' + std::to_string(ampId) +
                                ' ' + std::to_string(channel) + ' ' + value + ")\n";
    dataStream_ << message << std::flush;
}

} // namespace egiamp
