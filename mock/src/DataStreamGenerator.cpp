#include "DataStreamGenerator.h"
#include "ByteSwap.h"
#include <chrono>
#include <cstring>
#include <iostream>

namespace mock {

DataStreamGenerator::DataStreamGenerator(asio::io_context& io_context, uint16_t port,
                                         std::shared_ptr<MockAmplifier> amplifier)
    : io_context_(io_context)
    , acceptor_(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
    , amplifier_(amplifier)
{
}

DataStreamGenerator::~DataStreamGenerator() {
    stop();
}

void DataStreamGenerator::start() {
    running_ = true;
    acceptConnections();

    // Start streaming thread
    streamThread_ = std::make_unique<std::thread>(&DataStreamGenerator::streamingThread, this);

    std::cout << "[DataStreamGenerator] Listening on port " << acceptor_.local_endpoint().port() << std::endl;
}

void DataStreamGenerator::stop() {
    running_ = false;
    listening_ = false;

    asio::error_code ec;
    acceptor_.close(ec);

    if (streamThread_ && streamThread_->joinable()) {
        streamThread_->join();
    }

    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (auto& client : clients_) {
        client->close(ec);
    }
    clients_.clear();
}

void DataStreamGenerator::acceptConnections() {
    auto socket = std::make_shared<asio::ip::tcp::socket>(io_context_);
    acceptor_.async_accept(*socket, [this, socket](const asio::error_code& ec) {
        if (!ec && running_) {
            std::cout << "[DataStreamGenerator] Client connected" << std::endl;
            handleConnection(socket);
        }
        if (running_) {
            acceptConnections();
        }
    });
}

void DataStreamGenerator::handleConnection(std::shared_ptr<asio::ip::tcp::socket> socket) {
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_.insert(socket);
    }

    // Read commands from client (for cmd_ListenToAmp, etc.)
    readFromClient(socket);
}

void DataStreamGenerator::readFromClient(std::shared_ptr<asio::ip::tcp::socket> socket) {
    auto buffer = std::make_shared<asio::streambuf>();
    asio::async_read_until(*socket, *buffer, '\n',
        [this, socket, buffer](const asio::error_code& ec, std::size_t bytes_transferred) {
            if (ec) {
                removeClient(socket);
            } else if (running_) {
                std::istream is(buffer.get());
                std::string line;
                std::getline(is, line);

                // Remove trailing carriage return if present
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                std::cout << "[DataStreamGenerator] Received: " << line << std::endl;

                // Parse cmd_ListenToAmp or cmd_StopListeningToAmp
                // Note: Real amp server doesn't send text responses on data port,
                // it just starts/stops streaming binary data
                if (line.find("cmd_ListenToAmp") != std::string::npos) {
                    listening_ = true;
                } else if (line.find("cmd_StopListeningToAmp") != std::string::npos) {
                    listening_ = false;
                }

                readFromClient(socket);
            }
        });
}

void DataStreamGenerator::removeClient(std::shared_ptr<asio::ip::tcp::socket> socket) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    clients_.erase(socket);
    asio::error_code ec;
    socket->close(ec);
    std::cout << "[DataStreamGenerator] Client disconnected" << std::endl;
}

void DataStreamGenerator::startListening(int64_t ampId) {
    listeningAmpId_ = ampId;
    listening_ = true;
}

void DataStreamGenerator::stopListening(int64_t ampId) {
    listening_ = false;
}

void DataStreamGenerator::streamingThread() {
    // Pace to a steady deadline so the effective sample rate is accurate. Each
    // packet carries `samplesPerPacket` samples (~5 ms worth) and must therefore
    // span exactly samplesPerPacket/rate seconds. Computing the interval that way
    // (rather than a fixed 5 ms) keeps non-1000 rates exact too: e.g. 250 Hz ->
    // 1 sample / 4 ms, 500 Hz -> 2 samples / 4 ms. sleep_until compensates for the
    // time spent generating/sending, which sleep_for did not.
    auto nextDeadline = std::chrono::steady_clock::now();

    while (running_) {
        if (amplifier_->isStreaming() && listening_) {
            sendDataToClients();
        }

        const int rate = amplifier_->state().activeRate();
        int samplesPerPacket = (rate * 5) / 1000;
        if (samplesPerPacket < 1) samplesPerPacket = 1;
        const auto interval = std::chrono::nanoseconds(
            static_cast<int64_t>(1'000'000'000LL * samplesPerPacket / rate));

        nextDeadline += interval;
        const auto now = std::chrono::steady_clock::now();
        if (nextDeadline < now) {
            nextDeadline = now;  // fell behind (e.g. stall) — resync, don't burst
        }
        std::this_thread::sleep_until(nextDeadline);
    }
}

void DataStreamGenerator::sendDataToClients() {
    std::vector<uint8_t> buffer;
    generateSyntheticData(buffer);

    if (buffer.empty()) return;

    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (auto it = clients_.begin(); it != clients_.end(); ) {
        auto& socket = *it;
        asio::error_code ec;
        asio::write(*socket, asio::buffer(buffer), ec);
        if (ec) {
            std::cout << "[DataStreamGenerator] Error sending data: " << ec.message() << std::endl;
            it = clients_.erase(it);
        } else {
            ++it;
        }
    }
}

void DataStreamGenerator::generateSyntheticData(std::vector<uint8_t>& buffer) {
    const auto& state = amplifier_->state();

    // Calculate samples per packet based on sample rate
    // We send packets every 5ms, so samples = rate * 0.005
    // 1000 Hz = 5 samples, 2000 Hz = 10 samples, 4000 Hz = 20 samples, 8000 Hz = 40 samples
    int sampleRate = state.activeRate();
    int samplesPerPacket = (sampleRate * 5) / 1000;
    if (samplesPerPacket < 1) samplesPerPacket = 1;

    // Determine packet format
    if (state.packetFormat == PacketFormat::Format2) {
        // Packet Format 2
        size_t dataSize = samplesPerPacket * PACKET_FORMAT2_SIZE;
        buffer.resize(DATA_HEADER_SIZE + dataSize);

        // Write header (network byte order = big endian)
        AmpDataPacketHeader header;
        header.ampID = bswap64(static_cast<uint64_t>(state.ampId));
        header.length = bswap64(static_cast<uint64_t>(dataSize));
        std::memcpy(buffer.data(), &header, sizeof(header));

        // Generate sample packets
        for (int i = 0; i < samplesPerPacket; ++i) {
            PacketFormat2_SamplePacket packet;
            amplifier_->generatePacketFormat2(packet);

            // Copy packet to buffer (little endian, as per spec)
            std::memcpy(buffer.data() + DATA_HEADER_SIZE + i * PACKET_FORMAT2_SIZE,
                       &packet, PACKET_FORMAT2_SIZE);
        }
    } else {
        // Packet Format 1
        size_t dataSize = samplesPerPacket * PACKET_FORMAT1_SIZE;
        buffer.resize(DATA_HEADER_SIZE + dataSize);

        // Write header
        AmpDataPacketHeader header;
        header.ampID = bswap64(static_cast<uint64_t>(state.ampId));
        header.length = bswap64(static_cast<uint64_t>(dataSize));
        std::memcpy(buffer.data(), &header, sizeof(header));

        // Generate sample packets
        for (int i = 0; i < samplesPerPacket; ++i) {
            PacketFormat1_SamplePacket packet;
            amplifier_->generatePacketFormat1(packet);

            std::memcpy(buffer.data() + DATA_HEADER_SIZE + i * PACKET_FORMAT1_SIZE,
                       &packet, PACKET_FORMAT1_SIZE);
        }
    }
}

} // namespace mock
