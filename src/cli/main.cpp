#include <egiamp/AmpServerConfig.h>
#include <egiamp/EGIAmpClient.h>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
    std::atomic<bool> g_running{true};
    egiamp::EGIAmpClient* g_client = nullptr;
}

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\nShutting down..." << std::endl;
        g_running = false;
        if (g_client) {
            g_client->stopStreaming();
        }
    }
}

void printUsage(const char* programName) {
    std::cerr << "Usage: " << programName << " [options]\n"
              << "Options:\n"
              << "  --config <file>    Load configuration from file\n"
              << "  --address <addr>   AmpServer IP address (default: 10.10.10.51)\n"
              << "  --cmd-port <port>  Command port (default: 9877)\n"
              << "  --data-port <port> Data port (default: 9879)\n"
              << "  --amp-id <id>      Amplifier ID (default: 0)\n"
              << "  --sample-rate <hz> Sample rate (default: 1000)\n"
              << "  --fast-recovery    Use native rate for lower latency (no FPGA anti-alias filter)\n"
              << "  --impedance        Enable impedance testing mode (default: disabled)\n"
              << "  --native-format    Transmit raw int32 ADC counts instead of float microvolts\n"
              << "  --shutdown         Shutdown the Amp Server (terminates all connections)\n"
              << "  --help             Show this help message\n";
}

int main(int argc, char* argv[]) {
    egiamp::AmpServerConfig config;
    std::string configFile;
    bool shutdownMode = false;

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--config" && i + 1 < argc) {
            configFile = argv[++i];
        } else if (arg == "--address" && i + 1 < argc) {
            config.serverAddress = argv[++i];
        } else if (arg == "--cmd-port" && i + 1 < argc) {
            config.commandPort = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--data-port" && i + 1 < argc) {
            config.dataPort = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--amp-id" && i + 1 < argc) {
            config.amplifierId = std::stoi(argv[++i]);
        } else if (arg == "--sample-rate" && i + 1 < argc) {
            config.sampleRate = std::stoi(argv[++i]);
            config.forceSampleRate = true;  // User explicitly requested this rate
        } else if (arg == "--fast-recovery") {
            config.fastRecovery = true;
            config.forceSampleRate = true;  // Fast recovery implies forcing the rate
        } else if (arg == "--impedance") {
            config.impedance = true;
        } else if (arg == "--native-format") {
            config.nativeFormat = true;
        } else if (arg == "--shutdown") {
            shutdownMode = true;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    // Load config file if specified
    if (!configFile.empty()) {
        try {
            config = egiamp::AmpServerConfig::loadFromFile(configFile);
            std::cout << "Loaded configuration from: " << configFile << std::endl;
        } catch (const egiamp::ConfigError& e) {
            std::cerr << "Error loading config: " << e.what() << std::endl;
            return 1;
        }
    }

    // Set up signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Create client
    egiamp::EGIAmpClient client;
    g_client = &client;

    client.setConfig(config);

    // Set up callbacks
    client.setStatusCallback([](const std::string& msg) {
        std::cout << msg << std::flush;
    });

    client.setErrorCallback([](const std::string& msg) {
        std::cerr << "ERROR: " << msg << std::endl;
    });

    client.setChannelCountCallback([](int count) {
        std::cout << "Channel count: " << count << std::endl;
    });

    // Handle shutdown mode
    if (shutdownMode) {
        std::cout << "WARNING: This will terminate the Amp Server process.\n"
                  << "         All clients connected to the amplifier will be disconnected.\n"
                  << "         Address: " << config.serverAddress << "\n"
                  << "         Command Port: " << config.commandPort << "\n\n"
                  << "Are you sure you want to proceed? (y/N): ";

        std::string response;
        std::getline(std::cin, response);

        if (response != "y" && response != "Y") {
            std::cout << "Shutdown cancelled.\n";
            return 0;
        }

        if (!client.connect()) {
            std::cerr << "Failed to connect to AmpServer at "
                      << config.serverAddress << std::endl;
            return 1;
        }

        bool success = client.shutdownAmpServer();
        client.disconnect();

        return success ? 0 : 1;
    }

    // Print connection info
    std::cout << "EGI AmpServer CLI\n"
              << "  Address: " << config.serverAddress << "\n"
              << "  Command Port: " << config.commandPort << "\n"
              << "  Data Port: " << config.dataPort << "\n"
              << "  Amplifier ID: " << config.amplifierId << "\n"
              << "  Sample Rate: " << config.sampleRate << " Hz\n"
              << "  Impedance Mode: " << (config.impedance ? "enabled" : "disabled") << "\n"
              << "  Data Format: " << (config.nativeFormat ? "native (int32 counts)" : "microvolts (float32)") << "\n"
              << "Press Ctrl+C to stop.\n\n";

    // Connect and stream
    if (!client.connect()) {
        std::cerr << "Failed to connect to AmpServer at "
                  << config.serverAddress << std::endl;
        return 1;
    }

    std::cout << "Connected to AmpServer.\n";

    if (!client.startStreaming()) {
        std::cerr << "Failed to start streaming" << std::endl;
        client.disconnect();
        return 1;
    }

    // Wait for shutdown signal
    while (g_running && client.isStreaming()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    client.stopStreaming();
    client.disconnect();

    std::cout << "Disconnected.\n";
    return 0;
}
