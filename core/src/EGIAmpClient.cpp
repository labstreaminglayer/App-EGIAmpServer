#include "egiamp/EGIAmpClient.h"
#include "egiamp/Endian.h"

#include <chrono>
#include <iostream>
#include <regex>
#include <sstream>

namespace egiamp {

EGIAmpClient::EGIAmpClient() = default;

EGIAmpClient::~EGIAmpClient() {
    stopStreaming();
    disconnect();
}

void EGIAmpClient::setConfig(const AmpServerConfig& config) {
    config_ = config;
}

bool EGIAmpClient::connect() {
    return connection_.connect(config_.serverAddress,
                               config_.commandPort,
                               config_.notificationPort,
                               config_.dataPort);
}

void EGIAmpClient::disconnect() {
    connection_.disconnect();
}

bool EGIAmpClient::isConnected() const {
    return connection_.isConnected();
}

bool EGIAmpClient::isStreaming() const {
    return readerThread_ != nullptr;
}

void EGIAmpClient::emitStatus(const std::string& message) {
    if (statusCallback_) {
        statusCallback_(message);
    }
}

void EGIAmpClient::emitError(const std::string& message) {
    if (errorCallback_) {
        errorCallback_(message);
    }
}

void EGIAmpClient::emitChannelCount(int count) {
    if (channelCountCallback_) {
        channelCountCallback_(count);
    }
}

bool EGIAmpClient::queryAmplifierDetails() {
    try {
        std::string response = connection_.sendCommand(
            "cmd_GetAmpDetails", config_.amplifierId, 0, "0");

        emitStatus("__________________________\n  Amplifier Details\n__________________________");

        std::regex token(R"(\((\w+)\s+([^()]+)\))");
        std::smatch match;

        while (std::regex_search(response, match, token)) {
            std::string key = match[1].str();
            std::string value = match[2].str();

            if (key.find("packet_format") != std::string::npos) {
                details_.packetType = static_cast<PacketType>(std::stoi(value));
                emitStatus("    Packet Format: " + value);

            } else if (key.find("number_of_channels") != std::string::npos) {
                details_.channelCount = std::stoi(value);
                emitStatus("    Channel Count: " + value);

            } else if (key.find("amp_type") != std::string::npos) {
                if (value.find("NA300") != std::string::npos) {
                    details_.amplifierType = AmplifierType::NA300;
                } else if (value.find("NA400") != std::string::npos) {
                    details_.amplifierType = AmplifierType::NA400;
                }

            } else if (key.find("legacy_board") != std::string::npos) {
                // NA410 is differentiated from NA400 by legacy_board
                if (value.find("true") != std::string::npos &&
                    details_.amplifierType == AmplifierType::Unknown) {
                    details_.amplifierType = AmplifierType::NA410;
                }

            } else if (key.find("serial_number") != std::string::npos) {
                details_.serialNumber = value;
                emitStatus("    Serial Number: " + value);

            } else if (key.find("system_version") != std::string::npos) {
                details_.firmwareVersion = value;
                emitStatus("    Firmware Version: " + value);
            }

            response = match.suffix().str();
        }

        emitStatus(std::string("    Amplifier Type: ") + amplifierTypeName(details_.amplifierType));
        emitStatus("__________________________\n__________________________\n");

        details_.scalingFactor = getScalingFactor(details_.amplifierType);

        return true;
    } catch (...) {
        return false;
    }
}

bool EGIAmpClient::initAmplifier() {
    emitStatus("Initializing Amplifier...\n");

    int ampId = config_.amplifierId;
    std::string sampleRate = std::to_string(config_.sampleRate);

    // Stop and power off first to reset state
    connection_.sendCommand("cmd_Stop", ampId, 0, "0");
    connection_.sendCommand("cmd_SetPower", ampId, 0, "0");

    // Set sample rate
    connection_.sendCommand("cmd_SetDecimatedRate", ampId, 0, sampleRate);

    // Power on
    connection_.sendCommand("cmd_SetPower", ampId, 0, "1");

    // Start
    connection_.sendCommand("cmd_Start", ampId, 0, "0");

    // Set default acquisition state
    connection_.sendCommand("cmd_DefaultAcquisitionState", ampId, 0, "0");

    return true;
}

void EGIAmpClient::haltAmplifier() {
    emitStatus("Stopping stream...\n");
    try {
        connection_.sendDatastreamCommand("cmd_StopListeningToAmp",
                                          config_.amplifierId, 0, "0");
        connection_.sendCommand("cmd_Stop", config_.amplifierId, 0, "0");
        connection_.sendCommand("cmd_SetPower", config_.amplifierId, 0, "0");
        emitStatus("Stream Stopped.\n");
    } catch (...) {}
    stopFlag_ = false;
}

bool EGIAmpClient::startStreaming() {
    if (isStreaming()) {
        return false;
    }

    stopFlag_ = false;

    if (!queryAmplifierDetails()) {
        emitError("Failed to get amplifier details");
        return false;
    }

    initAmplifier();

    // Send listen command
    connection_.sendDatastreamCommand("cmd_ListenToAmp",
                                      config_.amplifierId, 0, "0");

    if (details_.channelCount != 0) {
        emitChannelCount(details_.channelCount);
    }

    // Start notification thread
    notificationThread_ = std::make_unique<std::thread>([this]() {
        try {
            processNotifications();
        } catch (...) {}
    });

    // Start reader thread
    readerThread_ = std::make_unique<std::thread>([this]() {
        emitStatus("Amplifier initialized.\n");
        try {
            if (details_.packetType == PacketType::Format1) {
                readPacketFormat1();
            } else {
                readPacketFormat2();
            }
        } catch (const std::exception& ex) {
            emitError(std::string("Error while reading data stream: ") + ex.what());
        }
        haltAmplifier();
    });

    return true;
}

void EGIAmpClient::stopStreaming() {
    stopFlag_ = true;

    if (readerThread_) {
        readerThread_->join();
        readerThread_.reset();
    }

    if (notificationThread_) {
        notificationThread_->join();
        notificationThread_.reset();
    }
}

void EGIAmpClient::run() {
    if (!connect()) {
        emitError("Could not connect to AmpServer. Please check network settings.");
        return;
    }

    if (!startStreaming()) {
        emitError("Failed to start streaming");
        disconnect();
        return;
    }

    // Wait for reader thread to finish
    if (readerThread_) {
        readerThread_->join();
        readerThread_.reset();
    }

    if (notificationThread_) {
        notificationThread_->join();
        notificationThread_.reset();
    }

    disconnect();
}

void EGIAmpClient::processNotifications() {
    auto& stream = connection_.notificationStream();

    while (stream.good() && !stopFlag_) {
        connection_.setNotificationStreamTimeout(std::chrono::seconds(1));
        char response[4096];
        stream.getline(response, sizeof(response));

        if (std::string(response).length() > 0) {
            emitStatus("__________________________\n  Notification Received\n    " +
                       std::string(response) + "\n__________________________\n");
        }
    }
}

void EGIAmpClient::readPacketFormat2() {
    auto& stream = connection_.dataStream();
    uint64_t lastPacketCounter = 0;
    int nChannels = details_.channelCount;
    bool firstPacketReceived = false;

    stream.clear();
    emitStatus("Starting stream.\n");

    while (stream.good() && !stopFlag_) {
        AmpDataPacketHeader header;
        stream.clear();
        connection_.setDataStreamTimeout(std::chrono::seconds(5));
        stream.read(reinterpret_cast<char*>(&header), sizeof(header));

        header.ampID = big_to_native(header.ampID);
        header.length = big_to_native(header.length);

        int nSamples = header.length / sizeof(PacketFormat2);
        int uniquePackets = 0;

        for (int s = 0; s < nSamples && stream.good(); s++) {
            PacketFormat2 packet;
            stream.read(reinterpret_cast<char*>(&packet), sizeof(PacketFormat2));

            if (!streamer_.hasOutlet()) {
                // Determine channel count from net code
                int detectedChannels = getChannelCountFromNetCode(
                    static_cast<NetCode>(packet.netCode));
                if (detectedChannels > 0) {
                    nChannels = detectedChannels;
                }

                emitChannelCount(nChannels);

                // Create LSL outlet
                std::string streamName = "EGI NetAmp " + std::to_string(header.ampID);
                streamer_.createOutlet(streamName, nChannels,
                                       config_.sampleRate, config_.serverAddress);
            }

            // Check for dropped or duplicate packets
            if (packet.packetCounter != 0 &&
                packet.packetCounter != lastPacketCounter + 1 &&
                packet.packetCounter != lastPacketCounter &&
                lastPacketCounter != 0) {
                emitStatus("Packet(s) Dropped: " +
                           std::to_string(packet.packetCounter - lastPacketCounter));
            } else if (firstPacketReceived && packet.packetCounter == lastPacketCounter) {
                // For sample rates < 1000, duplicates are sent - skip them
                continue;
            }

            if (lastPacketCounter == 0 && !firstPacketReceived) {
                emitStatus("Stream Started.\n");
            }

            if (lastPacketCounter == 0) {
                firstPacketReceived = true;
            }

            lastPacketCounter = packet.packetCounter;
            uniquePackets++;

            // Timestamp logging (periodic)
            if (lastTimeStamp_ != 0 &&
                (packet.packetCounter % (config_.sampleRate / 2)) == 0) {
                lastTimeStamp_ = packet.timeStamp;
                lastPacketCounterWithTimeStamp_ = packet.packetCounter;
            } else if (lastTimeStamp_ == 0) {
                emitStatus("Time Stamp: " + std::to_string(packet.timeStamp));
                lastTimeStamp_ = packet.timeStamp;
                lastPacketCounterWithTimeStamp_ = packet.packetCounter;
            }

            // Convert and push sample (PacketFormat2 is little endian natively)
            std::vector<float> samples;
            samples.reserve(nChannels);
            for (int ch = 0; ch < nChannels; ch++) {
                samples.push_back(static_cast<float>(packet.eegData[ch]) *
                                  details_.scalingFactor);
            }
            streamer_.pushSample(samples);
        }
    }

    if (!stream.good() && !stopFlag_) {
        emitError("The stream was lost.");
    }
}

void EGIAmpClient::readPacketFormat1() {
    auto& stream = connection_.dataStream();
    int nChannels = details_.channelCount;
    bool firstPacketReceived = false;

    stream.clear();
    emitStatus("Starting stream.\n");

    while (stream.good() && !stopFlag_) {
        AmpDataPacketHeader header;
        stream.read(reinterpret_cast<char*>(&header), sizeof(header));

        header.ampID = big_to_native(header.ampID);
        header.length = big_to_native(header.length);

        int nSamples = header.length / sizeof(PacketFormat1);

        for (int s = 0; s < nSamples && stream.good(); s++) {
            PacketFormat1 packet;
            stream.clear();
            connection_.setDataStreamTimeout(std::chrono::seconds(1));
            stream.read(reinterpret_cast<char*>(&packet), sizeof(PacketFormat1));

            if (!firstPacketReceived) {
                firstPacketReceived = true;
                emitStatus("Stream Started.\n");

                // Extract net code from header
                auto* headerBytes = reinterpret_cast<uint8_t*>(packet.header);
                uint8_t netCode = (headerBytes[26] & 0x78) >> 3;

                int detectedChannels = getChannelCountFromNetCode(
                    static_cast<NetCode>(netCode));
                if (detectedChannels > 0) {
                    nChannels = detectedChannels;
                }

                emitChannelCount(nChannels);

                // Create LSL outlet
                std::string streamName = "EGI NetAmp " + std::to_string(header.ampID);
                streamer_.createOutlet(streamName, nChannels,
                                       config_.sampleRate, config_.serverAddress);
            }

            // Convert endianness and push sample
            std::vector<float> sample;
            sample.reserve(nChannels);
            for (int i = 0; i < nChannels; i++) {
                float val = packet.eeg[i];
                big_to_native_inplace(val);
                sample.push_back(val);
            }
            streamer_.pushSample(sample);
        }
    }

    if (!stream.good() && !stopFlag_) {
        emitError("The stream was lost.");
    }
}

} // namespace egiamp
