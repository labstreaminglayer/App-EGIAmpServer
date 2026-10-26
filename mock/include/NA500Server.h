#ifndef MOCK_NA500SERVER_H
#define MOCK_NA500SERVER_H

#include "MockAmplifier.h"

#include <asio.hpp>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mock {

// Emulates the AmpServer embedded in a Net Amps 500 (GES 500), as observed on
// the wire with firmware 0.4.9 (scripts/ampserver_conformance.py records it).
// It is session based, unlike the NA400's AmpServer:
//  - each port accepts one client per session; later connections are never served;
//  - closing the command connection ends the session: streaming stops, the amp
//    powers off, the notification and data clients are dropped;
//  - each TCP segment on the command port is one command; an unparsable or
//    unknown command re-runs the previous one;
//  - data flows to the data client without cmd_ListenToAmp, 8 samples per block;
//  - a client that stops reading loses samples once a few blocks are queued.
class NA500Server {
public:
    NA500Server(std::shared_ptr<MockAmplifier> amplifier,
                uint16_t cmdPort, uint16_t notifyPort, uint16_t dataPort);
    ~NA500Server();

    // Serve sessions until `running` becomes false.
    void run(const std::atomic<bool>& running);

private:
    using Socket = asio::ip::tcp::socket;

    void session(const std::atomic<bool>& running);
    std::vector<std::string> execute(const std::string& name, long long channel, long long value);
    void notify(const std::string& line);
    void streamLoop();
    void sendLoop();
    int64_t ampClockMicros() const;

    asio::io_context io_;
    std::shared_ptr<MockAmplifier> amp_;
    uint16_t cmdPort_, notifyPort_, dataPort_;

    std::mutex socketMutex_;
    std::shared_ptr<Socket> notifySocket_, dataSocket_;

    std::atomic<bool> stopping_{false};
    std::atomic<bool> streaming_{false};
    std::atomic<bool> dinEnabled_{false};
    std::thread streamThread_;
    // Full blocks waiting to be written to the data client. Bounded like the
    // device's send ring: while the client is not reading, new blocks are lost.
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::deque<std::vector<uint8_t>> queue_;
    std::thread sendThread_;

    // The previous command, re-run when a segment does not parse.
    std::string lastName_ = "cmd_None";
    long long lastValues_[3] = {0, 0, 0};
    std::vector<std::string> pendingNotifications_;  // sent after the command's replies

    // Both persist across sessions, as on the device.
    std::atomic<uint64_t> packetCounter_{0};
    std::atomic<int64_t> clockOffsetUs_;
    std::atomic<int64_t> streamStartUs_{0};
};

} // namespace mock

#endif // MOCK_NA500SERVER_H
