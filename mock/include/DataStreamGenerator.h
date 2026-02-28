#ifndef MOCK_DATASTREAMGENERATOR_H
#define MOCK_DATASTREAMGENERATOR_H

#include "MockAmplifier.h"
#include <asio.hpp>
#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

namespace mock {

class DataStreamGenerator : public std::enable_shared_from_this<DataStreamGenerator> {
public:
    DataStreamGenerator(asio::io_context& io_context, uint16_t port,
                        std::shared_ptr<MockAmplifier> amplifier);
    ~DataStreamGenerator();

    void start();
    void stop();

    // Called when a client wants to listen to the amplifier
    void startListening(int64_t ampId);
    void stopListening(int64_t ampId);

private:
    void acceptConnections();
    void handleConnection(std::shared_ptr<asio::ip::tcp::socket> socket);
    void readFromClient(std::shared_ptr<asio::ip::tcp::socket> socket);
    void removeClient(std::shared_ptr<asio::ip::tcp::socket> socket);

    void streamingThread();
    void sendDataToClients();

    // Generate synthetic EEG data
    void generateSyntheticData(std::vector<uint8_t>& buffer);

    asio::io_context& io_context_;
    asio::ip::tcp::acceptor acceptor_;
    std::shared_ptr<MockAmplifier> amplifier_;

    std::atomic<bool> running_{false};
    std::atomic<bool> listening_{false};
    int64_t listeningAmpId_ = 0;

    std::mutex clientsMutex_;
    std::set<std::shared_ptr<asio::ip::tcp::socket>> clients_;

    std::unique_ptr<std::thread> streamThread_;
    std::mutex streamMutex_;
};

} // namespace mock

#endif // MOCK_DATASTREAMGENERATOR_H
