#include "ECIHandler.h"
#include "ByteSwap.h"
#include <cstring>
#include <iostream>

namespace mock {

ECIHandler::ECIHandler(asio::io_context& io_context, uint16_t port,
                       std::shared_ptr<MockAmplifier> amplifier)
    : io_context_(io_context)
    , acceptor_(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
    , amplifier_(amplifier)
{
}

ECIHandler::~ECIHandler() {
    stop();
}

void ECIHandler::start() {
    running_ = true;
    acceptConnections();
    std::cout << "[ECIHandler] Listening on port " << acceptor_.local_endpoint().port() << std::endl;
}

void ECIHandler::stop() {
    running_ = false;
    asio::error_code ec;
    acceptor_.close(ec);
}

void ECIHandler::acceptConnections() {
    auto socket = std::make_shared<asio::ip::tcp::socket>(io_context_);
    acceptor_.async_accept(*socket, [this, socket](const asio::error_code& ec) {
        if (!ec && running_) {
            std::cout << "[ECIHandler] Client connected" << std::endl;
            // Handle connection in a new thread to not block the io_context
            std::thread([this, socket]() {
                processClient(socket);
            }).detach();
        }
        if (running_) {
            acceptConnections();
        }
    });
}

void ECIHandler::handleConnection(std::shared_ptr<asio::ip::tcp::socket> socket) {
    processClient(socket);
}

void ECIHandler::processClient(std::shared_ptr<asio::ip::tcp::socket> socket) {
    char cmd;
    while (running_) {
        if (!readBytes(socket, &cmd, 1)) {
            break;
        }

        std::cout << "[ECIHandler] Received command: " << cmd << std::endl;

        switch (cmd) {
            case eci::CMD_QUERY: {
                // Read 4-byte machine type
                char machineType[5] = {0};
                if (readBytes(socket, machineType, 4)) {
                    handleQuery(socket, std::string(machineType, 4));
                }
                break;
            }

            case eci::CMD_NEW_QUERY:
                handleNewQuery(socket);
                break;

            case eci::CMD_EXIT:
                handleExit(socket);
                return;  // End session

            case eci::CMD_BEGIN_RECORDING:
                handleBeginRecording(socket);
                break;

            case eci::CMD_END_RECORDING:
                handleEndRecording(socket);
                break;

            case eci::CMD_ATTENTION:
                handleAttention(socket);
                break;

            case eci::CMD_CLOCK_SYNCH: {
                int32_t clientTime;
                if (readBytes(socket, &clientTime, 4)) {
                    handleClockSynch(socket, swapIfNeeded(clientTime));
                }
                break;
            }

            case eci::CMD_NTP_CLOCK_SYNCH: {
                int32_t clientTime;
                if (readBytes(socket, &clientTime, 4)) {
                    handleNTPClockSynch(socket, swapIfNeeded(clientTime));
                }
                break;
            }

            case eci::CMD_NTP_RETURN_SYNCH:
                handleNTPReturnSynch(socket);
                break;

            case eci::CMD_EVENT_DATA:
                handleEventData(socket);
                break;

            default:
                std::cerr << "[ECIHandler] Unknown command: " << cmd << std::endl;
                sendFailure(socket);
                break;
        }
    }
}

void ECIHandler::handleQuery(std::shared_ptr<asio::ip::tcp::socket> socket,
                              const std::string& machineType) {
    std::cout << "[ECIHandler] Query with machine type: " << machineType << std::endl;

    // Set byte order based on machine type
    if (machineType == eci::MACHINE_INTEL) {
        bigEndian_ = false;
    } else {
        bigEndian_ = true;  // MAC-, UNIX are big-endian
    }

    sendIdentify(socket);
}

void ECIHandler::handleNewQuery(std::shared_ptr<asio::ip::tcp::socket> socket) {
    std::cout << "[ECIHandler] New query" << std::endl;

    // Determine client's byte order from OS type (for new query, we assume little-endian modern systems)
    bigEndian_ = false;

    sendIdentify(socket);
}

void ECIHandler::handleExit(std::shared_ptr<asio::ip::tcp::socket> socket) {
    std::cout << "[ECIHandler] Exit" << std::endl;
    sendOK(socket);
}

void ECIHandler::handleBeginRecording(std::shared_ptr<asio::ip::tcp::socket> socket) {
    std::cout << "[ECIHandler] Begin recording" << std::endl;

    if (!amplifier_->isPowered()) {
        // No recording device
        char response = eci::RESP_NO_RECORDER;
        asio::write(*socket, asio::buffer(&response, 1));
        return;
    }

    recording_ = true;
    recordingStartTime_ = std::chrono::steady_clock::now();
    amplifier_->start();
    sendOK(socket);
}

void ECIHandler::handleEndRecording(std::shared_ptr<asio::ip::tcp::socket> socket) {
    std::cout << "[ECIHandler] End recording" << std::endl;
    recording_ = false;
    amplifier_->stop();
    sendOK(socket);
}

void ECIHandler::handleAttention(std::shared_ptr<asio::ip::tcp::socket> socket) {
    std::cout << "[ECIHandler] Attention" << std::endl;
    // Prepare for sync - nothing special to do in mock
    sendOK(socket);
}

void ECIHandler::handleClockSynch(std::shared_ptr<asio::ip::tcp::socket> socket, int32_t clientTime) {
    std::cout << "[ECIHandler] Clock sync, client time: " << clientTime << std::endl;

    auto now = std::chrono::steady_clock::now();
    syncTime_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    clientSyncTime_ = clientTime;

    sendOK(socket);
}

void ECIHandler::handleNTPClockSynch(std::shared_ptr<asio::ip::tcp::socket> socket, int32_t clientTime) {
    std::cout << "[ECIHandler] NTP clock sync, client time: " << clientTime << std::endl;

    auto now = std::chrono::steady_clock::now();
    syncTime_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    clientSyncTime_ = clientTime;

    sendOK(socket);
}

void ECIHandler::handleNTPReturnSynch(std::shared_ptr<asio::ip::tcp::socket> socket) {
    std::cout << "[ECIHandler] NTP return sync" << std::endl;

    auto now = std::chrono::steady_clock::now();
    syncTime_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    // Send back 8-byte NTP time (simplified - just use our sync time)
    int64_t ntpTime = syncTime_;
    if (bigEndian_) {
        ntpTime = bswap64(ntpTime);
    }
    asio::write(*socket, asio::buffer(&ntpTime, 8));
}

void ECIHandler::handleEventData(std::shared_ptr<asio::ip::tcp::socket> socket) {
    // Read data length (2 bytes)
    uint16_t dataLength;
    if (!readBytes(socket, &dataLength, 2)) {
        sendFailure(socket);
        return;
    }
    dataLength = swapIfNeeded(dataLength);

    // Read event data
    std::vector<uint8_t> eventData(dataLength);
    if (!readBytes(socket, eventData.data(), dataLength)) {
        sendFailure(socket);
        return;
    }

    // Parse event (simplified - just acknowledge receipt)
    size_t offset = 0;

    // Start time (4 bytes)
    int32_t startTime;
    std::memcpy(&startTime, eventData.data() + offset, 4);
    startTime = swapIfNeeded(startTime);
    offset += 4;

    // Duration (4 bytes)
    int32_t duration;
    std::memcpy(&duration, eventData.data() + offset, 4);
    duration = swapIfNeeded(duration);
    offset += 4;

    // Event code (4 bytes)
    char eventCode[5] = {0};
    std::memcpy(eventCode, eventData.data() + offset, 4);
    offset += 4;

    std::cout << "[ECIHandler] Event: code=" << eventCode
              << " start=" << startTime << "ms duration=" << duration << "ms" << std::endl;

    sendOK(socket);
}

void ECIHandler::sendOK(std::shared_ptr<asio::ip::tcp::socket> socket) {
    char response = eci::RESP_OK;
    asio::error_code ec;
    asio::write(*socket, asio::buffer(&response, 1), ec);
}

void ECIHandler::sendFailure(std::shared_ptr<asio::ip::tcp::socket> socket, int16_t errorCode) {
    char response[3];
    response[0] = eci::RESP_FAILURE;
    int16_t swapped = swapIfNeeded(errorCode);
    std::memcpy(response + 1, &swapped, 2);
    asio::error_code ec;
    asio::write(*socket, asio::buffer(response, 3), ec);
}

void ECIHandler::sendIdentify(std::shared_ptr<asio::ip::tcp::socket> socket) {
    char response[2];
    response[0] = eci::RESP_IDENTIFY;
    response[1] = eci::PROTOCOL_VERSION;
    asio::error_code ec;
    asio::write(*socket, asio::buffer(response, 2), ec);
}

bool ECIHandler::readBytes(std::shared_ptr<asio::ip::tcp::socket> socket, void* buffer, size_t count) {
    asio::error_code ec;
    size_t bytesRead = asio::read(*socket, asio::buffer(buffer, count), ec);
    return !ec && bytesRead == count;
}

int32_t ECIHandler::swapIfNeeded(int32_t value) const {
    if (bigEndian_) {
        return bswap32(value);
    }
    return value;
}

int16_t ECIHandler::swapIfNeeded(int16_t value) const {
    if (bigEndian_) {
        return bswap16(value);
    }
    return value;
}

} // namespace mock
