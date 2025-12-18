#ifndef MOCK_ECIHANDLER_H
#define MOCK_ECIHANDLER_H

#include "MockAmplifier.h"
#include <asio.hpp>
#include <chrono>
#include <memory>
#include <string>

namespace mock {

// ECI Protocol constants
namespace eci {
    // Commands (single byte)
    constexpr char CMD_QUERY = 'Q';           // Query with machine type
    constexpr char CMD_NEW_QUERY = 'Y';       // New query
    constexpr char CMD_EXIT = 'X';            // Exit
    constexpr char CMD_BEGIN_RECORDING = 'B'; // Begin recording
    constexpr char CMD_END_RECORDING = 'E';   // End recording
    constexpr char CMD_ATTENTION = 'A';       // Attention (prepare for sync)
    constexpr char CMD_CLOCK_SYNCH = 'T';     // Clock sync
    constexpr char CMD_NTP_CLOCK_SYNCH = 'N'; // NTP clock sync
    constexpr char CMD_NTP_RETURN_SYNCH = 'S';// NTP sync with return
    constexpr char CMD_EVENT_DATA = 'D';      // Event data

    // Responses
    constexpr char RESP_OK = 'Z';             // Success
    constexpr char RESP_FAILURE = 'F';        // Failure
    constexpr char RESP_NO_RECORDER = 'R';    // No recording device
    constexpr char RESP_IDENTIFY = 'I';       // Identify response

    // Machine types (legacy 4-char codes)
    constexpr const char* MACHINE_MAC = "MAC-";
    constexpr const char* MACHINE_UNIX = "UNIX";
    constexpr const char* MACHINE_INTEL = "NTEL";

    // Machine types (new single-char codes)
    constexpr char MACHINE_UNKNOWN = 'u';
    constexpr char MACHINE_I386 = 'i';
    constexpr char MACHINE_X86_64 = 'x';
    constexpr char MACHINE_PPC = 'p';

    // ECI protocol version
    constexpr uint8_t PROTOCOL_VERSION = 1;
}

// ECI Event structure
struct ECIEvent {
    int32_t startTime;      // ms from recording start
    int32_t duration;       // ms, minimum 1
    char eventCode[4];      // 4-char event code
    std::string label;      // up to 256 chars
    std::string description;// up to 256 chars

    struct Key {
        char key[4];
        char dataType[4];
        std::vector<uint8_t> data;
    };
    std::vector<Key> keys;
};

class ECIHandler : public std::enable_shared_from_this<ECIHandler> {
public:
    ECIHandler(asio::io_context& io_context, uint16_t port,
               std::shared_ptr<MockAmplifier> amplifier);
    ~ECIHandler();

    void start();
    void stop();

private:
    void acceptConnections();
    void handleConnection(std::shared_ptr<asio::ip::tcp::socket> socket);
    void processClient(std::shared_ptr<asio::ip::tcp::socket> socket);

    // Command handlers
    void handleQuery(std::shared_ptr<asio::ip::tcp::socket> socket,
                     const std::string& machineType);
    void handleNewQuery(std::shared_ptr<asio::ip::tcp::socket> socket);
    void handleExit(std::shared_ptr<asio::ip::tcp::socket> socket);
    void handleBeginRecording(std::shared_ptr<asio::ip::tcp::socket> socket);
    void handleEndRecording(std::shared_ptr<asio::ip::tcp::socket> socket);
    void handleAttention(std::shared_ptr<asio::ip::tcp::socket> socket);
    void handleClockSynch(std::shared_ptr<asio::ip::tcp::socket> socket, int32_t clientTime);
    void handleNTPClockSynch(std::shared_ptr<asio::ip::tcp::socket> socket, int32_t clientTime);
    void handleNTPReturnSynch(std::shared_ptr<asio::ip::tcp::socket> socket);
    void handleEventData(std::shared_ptr<asio::ip::tcp::socket> socket);

    // Response helpers
    void sendOK(std::shared_ptr<asio::ip::tcp::socket> socket);
    void sendFailure(std::shared_ptr<asio::ip::tcp::socket> socket, int16_t errorCode = 0);
    void sendIdentify(std::shared_ptr<asio::ip::tcp::socket> socket);

    // Utility
    bool readBytes(std::shared_ptr<asio::ip::tcp::socket> socket, void* buffer, size_t count);
    bool isBigEndian() const { return bigEndian_; }
    int32_t swapIfNeeded(int32_t value) const;
    int16_t swapIfNeeded(int16_t value) const;

    asio::io_context& io_context_;
    asio::ip::tcp::acceptor acceptor_;
    std::shared_ptr<MockAmplifier> amplifier_;
    std::atomic<bool> running_{false};

    // Session state
    bool bigEndian_ = true;       // Client byte order (MAC/UNIX = big, INTEL = little)
    bool recording_ = false;
    int64_t syncTime_ = 0;        // Server time at sync
    int64_t clientSyncTime_ = 0;  // Client time at sync
    std::chrono::steady_clock::time_point recordingStartTime_;
};

} // namespace mock

#endif // MOCK_ECIHANDLER_H
