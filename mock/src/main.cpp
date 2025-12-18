#include "AmpServerProtocol.h"
#include "CommandHandler.h"
#include "DataStreamGenerator.h"
#include "ECIHandler.h"
#include "MockAmplifier.h"
#include "NotificationHandler.h"

#include <csignal>
#include <iostream>
#include <memory>
#include <thread>

namespace {
    std::atomic<bool> g_running{true};

    void signalHandler(int signal) {
        std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
        g_running = false;
    }
}

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options]\n"
              << "\nOptions:\n"
              << "  -h, --help          Show this help message\n"
              << "  -a, --amp-type TYPE Amplifier type: NA300, NA400, NA410, NA500, GTEN200 (default: NA400)\n"
              << "  -n, --net-code CODE Net code: GSN64, GSN128, GSN256, HGSN32, etc. (default: GSN256)\n"
              << "  -s, --serial NUM    Serial number (default: MOCK12345678)\n"
              << "  -p, --physio STATUS Physio16 connection status: 0, 1, 2, or 3 (default: 0)\n"
              << "  --cmd-port PORT     Command port (default: 9877)\n"
              << "  --notify-port PORT  Notification port (default: 9878)\n"
              << "  --data-port PORT    Data stream port (default: 9879)\n"
              << "  --eci-port PORT     ECI port (default: 55513)\n"
              << "\nNet code values:\n"
              << "  GSN64, GSN128, GSN256      - Standard GSN nets\n"
              << "  HGSN32, HGSN64, HGSN128, HGSN256 - HydroCel GSN nets\n"
              << "  MGSN32, MGSN64, MGSN128, MGSN256 - MicroCel GSN nets\n"
              << "  TestConnector - Test connector (256 channels)\n"
              << std::endl;
}

mock::NetCode parseNetCode(const std::string& str) {
    if (str == "GSN64") return mock::NetCode::GSN64_2_0;
    if (str == "GSN128") return mock::NetCode::GSN128_2_0;
    if (str == "GSN256") return mock::NetCode::GSN256_2_0;
    if (str == "HGSN32") return mock::NetCode::HCGSN32_1_0;
    if (str == "HGSN64") return mock::NetCode::HCGSN64_1_0;
    if (str == "HGSN128") return mock::NetCode::HCGSN128_1_0;
    if (str == "HGSN256") return mock::NetCode::HCGSN256_1_0;
    if (str == "MGSN32") return mock::NetCode::MCGSN32_1_0;
    if (str == "MGSN64") return mock::NetCode::MCGSN64_1_0;
    if (str == "MGSN128") return mock::NetCode::MCGSN128_1_0;
    if (str == "MGSN256") return mock::NetCode::MCGSN256_1_0;
    if (str == "TestConnector") return mock::NetCode::TestConnector;
    return mock::NetCode::GSN256_2_0;  // default
}

mock::AmplifierType parseAmpType(const std::string& str) {
    if (str == "NA300") return mock::AmplifierType::NA300;
    if (str == "NA400") return mock::AmplifierType::NA400;
    if (str == "NA410") return mock::AmplifierType::NA410;
    if (str == "NA500") return mock::AmplifierType::NA500;
    if (str == "GTEN200") return mock::AmplifierType::GTEN200;
    return mock::AmplifierType::NA400;  // default
}

int main(int argc, char* argv[]) {
    // Default configuration
    mock::AmplifierType ampType = mock::AmplifierType::NA400;
    mock::NetCode netCode = mock::NetCode::GSN256_2_0;
    std::string serialNumber = "MOCK12345678";
    int physioStatus = 0;
    uint16_t cmdPort = mock::COMMAND_PORT;
    uint16_t notifyPort = mock::NOTIFICATION_PORT;
    uint16_t dataPort = mock::DATA_PORT;
    uint16_t eciPort = mock::ECI_PORT;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if ((arg == "-a" || arg == "--amp-type") && i + 1 < argc) {
            ampType = parseAmpType(argv[++i]);
        } else if ((arg == "-n" || arg == "--net-code") && i + 1 < argc) {
            netCode = parseNetCode(argv[++i]);
        } else if ((arg == "-s" || arg == "--serial") && i + 1 < argc) {
            serialNumber = argv[++i];
        } else if ((arg == "-p" || arg == "--physio") && i + 1 < argc) {
            physioStatus = std::stoi(argv[++i]);
        } else if (arg == "--cmd-port" && i + 1 < argc) {
            cmdPort = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--notify-port" && i + 1 < argc) {
            notifyPort = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--data-port" && i + 1 < argc) {
            dataPort = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--eci-port" && i + 1 < argc) {
            eciPort = static_cast<uint16_t>(std::stoi(argv[++i]));
        }
    }

    // Set up signal handling
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "=== Mock EGI Amp Server ===" << std::endl;
    std::cout << "Amplifier Type: " << mock::amplifierTypeName(ampType) << std::endl;
    std::cout << "Net Code: " << mock::netCodeName(netCode)
              << " (" << mock::getChannelCountFromNetCode(netCode) << " channels)" << std::endl;
    std::cout << "Serial Number: " << serialNumber << std::endl;
    std::cout << "Physio16 Status: " << physioStatus << std::endl;
    std::cout << std::endl;

    try {
        // Create IO context
        asio::io_context io_context;

        // Create mock amplifier
        auto amplifier = std::make_shared<mock::MockAmplifier>(0);
        amplifier->setAmplifierType(ampType);
        amplifier->setNetCode(netCode);
        amplifier->setSerialNumber(serialNumber);
        amplifier->setPhysioConnectionStatus(physioStatus);

        // Create handlers
        auto notificationHandler = std::make_shared<mock::NotificationHandler>(
            io_context, notifyPort, amplifier);

        auto commandHandler = std::make_shared<mock::CommandHandler>(
            io_context, cmdPort, amplifier, notificationHandler);

        auto dataStreamGenerator = std::make_shared<mock::DataStreamGenerator>(
            io_context, dataPort, amplifier);

        auto eciHandler = std::make_shared<mock::ECIHandler>(
            io_context, eciPort, amplifier);

        // Start all handlers
        notificationHandler->start();
        commandHandler->start();
        dataStreamGenerator->start();
        eciHandler->start();

        std::cout << "\nServer started. Press Ctrl+C to stop.\n" << std::endl;
        std::cout << "Ports:" << std::endl;
        std::cout << "  Command:      " << cmdPort << std::endl;
        std::cout << "  Notification: " << notifyPort << std::endl;
        std::cout << "  Data Stream:  " << dataPort << std::endl;
        std::cout << "  ECI:          " << eciPort << std::endl;
        std::cout << std::endl;

        // Run io_context in separate threads
        std::vector<std::thread> threads;
        const int numThreads = 4;
        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back([&io_context]() {
                io_context.run();
            });
        }

        // Wait for shutdown signal
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Stop handlers
        std::cout << "Stopping handlers..." << std::endl;
        commandHandler->stop();
        notificationHandler->stop();
        dataStreamGenerator->stop();
        eciHandler->stop();

        // Stop io_context
        io_context.stop();

        // Join threads
        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }

        std::cout << "Server stopped." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
