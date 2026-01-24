#include "egiamp/EGIAmpClient.h"
#include "egiamp/Endian.h"

#include <chrono>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <utility>

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

void EGIAmpClient::emitSensor(NetCode code) {
    if (sensorCallback_) {
        sensorCallback_(code);
    }
}

bool EGIAmpClient::commandCompleted(const std::string& response) {
    return response.find("(status complete)") != std::string::npos;
}

bool EGIAmpClient::cmd_ImpedanceAcquisitionState() {
    emitStatus("Enabling impedance mode...\n");

    const int ampId = config_.amplifierId;
    const std::pair<const char*, const char*> commands[] = {
        {"cmd_TurnAll10KOhms", "1"},
        {"cmd_SetReference10KOhms", "1"},
        {"cmd_SetSubjectGround", "1"},
        {"cmd_SetCurrentSource", "1"},
        {"cmd_TurnAllDriveSignals", "1"},
    };

    for (const auto& [command, value] : commands) {
        const std::string response = connection_.sendCommand(command, ampId, 0, value);
        if (!commandCompleted(response)) {
            throw std::runtime_error(std::string(command) + " failed: " + response);
        }
    }

    emitStatus("Impedance mode enabled.\n");
    return true;
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

bool EGIAmpClient::isAmplifierStreaming() {
    // Try to receive data with a short timeout to detect if amp is already running.
    // After detection, we reconnect the data stream to reset the buffer position.
    try {
        connection_.sendDatastreamCommand("cmd_ListenToAmp", config_.amplifierId, 0, "0");

        auto& stream = connection_.dataStream();
        stream.clear();
        connection_.setDataStreamTimeout(std::chrono::seconds(2));

        // Try to read a packet header
        AmpDataPacketHeader header;
        stream.read(reinterpret_cast<char*>(&header), sizeof(header));

        bool wasStreaming = stream.good();

        // Stop listening and reconnect the data stream to clear any buffered data
        connection_.sendDatastreamCommand("cmd_StopListeningToAmp", config_.amplifierId, 0, "0");
        connection_.reconnectDataStream();

        return wasStreaming;
    } catch (...) {
        // Timeout or error - amp is not streaming
        try {
            connection_.sendDatastreamCommand("cmd_StopListeningToAmp", config_.amplifierId, 0, "0");
            connection_.reconnectDataStream();
        } catch (...) {}
        return false;
    }
}

int EGIAmpClient::detectSampleRate() {
    // Read packets and calculate sample rate from unique packetCounter values.
    // At lower sample rates, duplicate packets are sent (same packetCounter),
    // so we count unique samples, not total packets received.
    // Also detects the sensor net code from the first packet.
    try {
        connection_.sendDatastreamCommand("cmd_ListenToAmp", config_.amplifierId, 0, "0");

        auto& stream = connection_.dataStream();
        stream.clear();
        connection_.setDataStreamTimeout(std::chrono::seconds(3));

        uint64_t firstTimestamp = 0;
        uint64_t lastTimestamp = 0;
        uint64_t lastPacketCounter = 0;
        int uniqueSampleCount = 0;
        bool netCodeCaptured = false;
        const int packetsToRead = 1000;  // Read ~1 second of packets

        for (int i = 0; i < packetsToRead && stream.good(); i++) {
            AmpDataPacketHeader header;
            stream.read(reinterpret_cast<char*>(&header), sizeof(header));
            header.ampID = big_to_native(header.ampID);
            header.length = big_to_native(header.length);

            int nSamples = header.length / sizeof(PacketFormat2);
            for (int s = 0; s < nSamples && stream.good(); s++) {
                PacketFormat2 packet;
                stream.read(reinterpret_cast<char*>(&packet), sizeof(packet));

                // Capture net code from the first packet
                if (!netCodeCaptured) {
                    detectedNetCode_ = static_cast<NetCode>(packet.netCode);
                    netCodeCaptured = true;
                }

                // Only count unique samples (different packetCounter)
                if (packet.packetCounter != lastPacketCounter) {
                    if (firstTimestamp == 0) {
                        firstTimestamp = packet.timeStamp;
                    }
                    lastTimestamp = packet.timeStamp;
                    lastPacketCounter = packet.packetCounter;
                    uniqueSampleCount++;
                }
            }
        }

        connection_.sendDatastreamCommand("cmd_StopListeningToAmp", config_.amplifierId, 0, "0");
        connection_.reconnectDataStream();

        if (uniqueSampleCount > 1 && lastTimestamp > firstTimestamp) {
            // timeStamp is in microseconds
            double durationUs = static_cast<double>(lastTimestamp - firstTimestamp);
            double durationSec = durationUs / 1000000.0;
            double rate = (uniqueSampleCount - 1) / durationSec;

            // Round to nearest standard rate (250, 500, 1000)
            if (rate < 375) return 250;
            if (rate < 750) return 500;
            return 1000;
        }

        return 0;  // Detection failed
    } catch (...) {
        try {
            connection_.sendDatastreamCommand("cmd_StopListeningToAmp", config_.amplifierId, 0, "0");
            connection_.reconnectDataStream();
        } catch (...) {}
        return 0;
    }
}

bool EGIAmpClient::queryAmpState() {
    // Query if amp is running and detect its sample rate and sensor
    ampWasRunning_ = isAmplifierStreaming();

    if (ampWasRunning_) {
        detectedSampleRate_ = detectSampleRate();
        emitStatus("Detected sample rate: " + std::to_string(detectedSampleRate_) + " Hz\n");
        emitStatus(std::string("Detected sensor: ") + netCodeName(detectedNetCode_) + "\n");
    } else {
        detectedSampleRate_ = 0;
        detectedNetCode_ = NetCode::Unknown;
    }

    return ampWasRunning_;
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

    // Configure acquisition state (impedance or default)
    if (config_.impedance) {
        try {
            cmd_ImpedanceAcquisitionState();
        } catch (const std::exception& ex) {
            emitError(std::string("Failed to configure impedance mode: ") + ex.what());
            return false;
        }
    } else {
        // Set default acquisition state when not in impedance mode
        connection_.sendCommand("cmd_DefaultAcquisitionState", ampId, 0, "0");
    }

    // Start
    connection_.sendCommand("cmd_Start", ampId, 0, "0");

    return true;
}

void EGIAmpClient::haltAmplifier() {
    emitStatus("Stopping stream...\n");
    try {
        connection_.sendDatastreamCommand("cmd_StopListeningToAmp",
                                          config_.amplifierId, 0, "0");
        if (weInitializedAmp_) {
            // Only stop/power off if we initialized the amp ourselves
            connection_.sendCommand("cmd_Stop", config_.amplifierId, 0, "0");
            connection_.sendCommand("cmd_SetPower", config_.amplifierId, 0, "0");
            emitStatus("Amplifier stopped.\n");
        }
        emitStatus("Stream Stopped.\n");
    } catch (...) {}

    // Close the LSL outlets so next session starts fresh
    streamer_.closeOutlet();
    impedanceStreamer_.closeOutlet();
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

    // Check if amplifier is already streaming data
    bool ampRunning = isAmplifierStreaming();

    if (ampRunning) {
        emitStatus("Amplifier is already running, skipping initialization.\n");
        weInitializedAmp_ = false;

        // Detect and use the actual sample rate
        int detectedRate = detectSampleRate();
        if (detectedRate > 0) {
            config_.sampleRate = detectedRate;
            emitStatus("Using detected sample rate: " + std::to_string(detectedRate) + " Hz\n");
        }
    } else {
        emitStatus("Amplifier is not running, initializing...\n");
        initAmplifier();
        weInitializedAmp_ = true;
    }

    // Start listening for data
    connection_.sendDatastreamCommand("cmd_ListenToAmp",
                                      config_.amplifierId, 0, "0");

    if (details_.channelCount != 0) {
        emitChannelCount(details_.channelCount);
    }

    // Subscribe to notifications
    connection_.sendCommand("cmd_ReceiveNotifications", config_.amplifierId, 0, "0");

    // Query Physio16 connection status
    connection_.sendCommand("cmd_GetPhysioConnectionStatus", config_.amplifierId, 0, "0");

    // Read notifications until we find physio status or timeout
    // (there may be other notifications like client_connected in the queue)
    physioConnectionStatus_ = 0;
    try {
        auto& notifStream = connection_.notificationStream();
        std::regex statusRegex(R"(ntn_PhysioConnectionStatus\s+\d+\s+\d+\s+\+(\d+))");

        for (int attempt = 0; attempt < 5; attempt++) {
            connection_.setNotificationStreamTimeout(std::chrono::milliseconds(500));
            char notifBuffer[4096];
            notifStream.getline(notifBuffer, sizeof(notifBuffer));

            std::string notification(notifBuffer);
            std::smatch match;
            if (std::regex_search(notification, match, statusRegex)) {
                physioConnectionStatus_ = std::stoi(match[1].str());
                emitStatus("Physio16 connection status: " + std::to_string(physioConnectionStatus_) +
                           " (" + (physioConnectionStatus_ == 0 ? "none" :
                                   physioConnectionStatus_ == 1 ? "port 1 - 16 channels" :
                                   physioConnectionStatus_ == 2 ? "port 2 - 16 channels" :
                                   "both ports - 32 channels") + ")\n");
                break;
            }
            // Not the physio notification, try again
        }

        if (physioConnectionStatus_ == 0) {
            emitStatus("Physio16: no device detected\n");
        }
    } catch (...) {
        emitStatus("Physio16: query timed out, assuming no device\n");
        physioConnectionStatus_ = 0;
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

bool EGIAmpClient::shutdownAmpServer() {
    try {
        emitStatus("Sending shutdown command to Amp Server...\n");
        connection_.sendCommand("cmd_Exit", 0, 0, "0");
        emitStatus("Amp Server shutdown command sent.\n");
        return true;
    } catch (const std::exception& ex) {
        emitError(std::string("Failed to send shutdown command: ") + ex.what());
        return false;
    } catch (...) {
        emitError("Failed to send shutdown command: unknown error");
        return false;
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

        std::string notification(response);
        if (notification.length() > 0) {
            emitStatus("__________________________\n  Notification Received\n    " +
                       notification + "\n__________________________\n");

            // Check for amp restart - sample rate may have changed
            if (notification.find("ntn_AmpStarted") != std::string::npos) {
                sampleRateChangeDetected_ = true;
            }
        }
    }
}

void EGIAmpClient::readPacketFormat2() {
    auto& stream = connection_.dataStream();
    uint64_t lastPacketCounter = 0;
    int nChannels = details_.channelCount;
    bool firstPacketReceived = false;

    // Sample rate change detection
    bool measuringNewRate = false;
    uint64_t rateCheckStartTimestamp = 0;
    uint64_t rateCheckStartCounter = 0;
    int rateCheckSampleCount = 0;
    const int RATE_CHECK_SAMPLES = 500;  // Samples to measure over

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
                // Determine channel count and sensor from net code
                details_.netCode = static_cast<NetCode>(packet.netCode);
                int detectedChannels = getChannelCountFromNetCode(details_.netCode);
                if (detectedChannels > 0) {
                    nChannels = detectedChannels;
                }

                emitStatus(std::string("Sensor: ") + netCodeName(details_.netCode) + "\n");
                emitSensor(details_.netCode);

                // Use the pre-queried Physio16 connection status
                // (queried in startStreaming() before notification thread started)
                int physioChannelCount = 0;
                if (physioConnectionStatus_ == 1 || physioConnectionStatus_ == 2) {
                    physioChannelCount = 16;
                } else if (physioConnectionStatus_ == 3) {
                    physioChannelCount = 32;
                }

                emitChannelCount(nChannels + physioChannelCount);

                // Create LSL outlet for EEG (+ Physio if connected)
                std::string streamName = "EGI NetAmp " + std::to_string(header.ampID);
                streamer_.createOutlet(streamName, nChannels, physioChannelCount,
                                       config_.sampleRate, config_.serverAddress, details_);

                // Create LSL outlet for impedance if enabled
                if (config_.impedance) {
                    std::string impedanceStreamName = streamName + " Impedance";
                    impedanceStreamer_.createImpedanceOutlet(impedanceStreamName, nChannels,
                                                             config_.serverAddress, details_);
                    emitStatus("Impedance stream created.\n");
                }
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

            // Sample rate change detection
            if (sampleRateChangeDetected_ && !measuringNewRate) {
                // Start measuring new rate
                measuringNewRate = true;
                rateCheckStartTimestamp = packet.timeStamp;
                rateCheckStartCounter = packet.packetCounter;
                rateCheckSampleCount = 0;
                emitStatus("Amp restarted detected, measuring new sample rate...\n");
            }

            if (measuringNewRate) {
                rateCheckSampleCount++;
                if (rateCheckSampleCount >= RATE_CHECK_SAMPLES) {
                    // Calculate new sample rate
                    uint64_t durationUs = packet.timeStamp - rateCheckStartTimestamp;
                    if (durationUs > 0) {
                        double durationSec = static_cast<double>(durationUs) / 1000000.0;
                        double measuredRate = (rateCheckSampleCount - 1) / durationSec;

                        // Snap to standard rates
                        int newRate;
                        if (measuredRate < 375) newRate = 250;
                        else if (measuredRate < 750) newRate = 500;
                        else newRate = 1000;

                        if (newRate != config_.sampleRate) {
                            emitStatus("Sample rate changed from " +
                                       std::to_string(config_.sampleRate) + " Hz to " +
                                       std::to_string(newRate) + " Hz, recreating LSL outlets...\n");
                            config_.sampleRate = newRate;

                            // Recreate LSL outlets with new rate
                            streamer_.closeOutlet();
                            std::string streamName = "EGI NetAmp " + std::to_string(header.ampID);
                            int physioChCount = (physioConnectionStatus_ == 3) ? 32 :
                                                (physioConnectionStatus_ > 0) ? 16 : 0;
                            streamer_.createOutlet(streamName, nChannels, physioChCount,
                                                   config_.sampleRate, config_.serverAddress, details_);

                            // Recreate impedance outlet if enabled
                            if (config_.impedance) {
                                impedanceStreamer_.closeOutlet();
                                std::string impedanceStreamName = streamName + " Impedance";
                                impedanceStreamer_.createImpedanceOutlet(impedanceStreamName, nChannels,
                                                                         config_.serverAddress, details_);
                            }
                        } else {
                            emitStatus("Sample rate unchanged at " +
                                       std::to_string(config_.sampleRate) + " Hz\n");
                        }
                    }
                    measuringNewRate = false;
                    sampleRateChangeDetected_ = false;
                }
            }

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

            // Convert and push sample to EEG stream (PacketFormat2 is little endian natively)
            std::vector<float> eegSamples;
            int physioChannels = (physioConnectionStatus_ == 3) ? 32 :
                                 (physioConnectionStatus_ > 0) ? 16 : 0;
            eegSamples.reserve(nChannels + physioChannels);
            for (int ch = 0; ch < nChannels; ch++) {
                eegSamples.push_back(static_cast<float>(packet.eegData[ch]) *
                                     details_.scalingFactor);
            }

            // Add PIB1 channels (if port 1 connected: status 1 or 3)
            // Channels 1-8 use negative scaling, 9-16 use positive scaling
            if (physioConnectionStatus_ & 0x01) {
                for (int ch = 0; ch < 8; ch++) {
                    eegSamples.push_back(static_cast<float>(packet.pib1_Data[ch]) *
                                         PHYSIO_SCALING_1_8);
                }
                for (int ch = 8; ch < 16; ch++) {
                    eegSamples.push_back(static_cast<float>(packet.pib1_Data[ch]) *
                                         PHYSIO_SCALING_9_16);
                }
            }

            // Add PIB2 channels (if port 2 connected: status 2 or 3)
            // Channels 1-8 use negative scaling, 9-16 use positive scaling
            if (physioConnectionStatus_ & 0x02) {
                for (int ch = 0; ch < 8; ch++) {
                    eegSamples.push_back(static_cast<float>(packet.pib2_Data[ch]) *
                                         PHYSIO_SCALING_1_8);
                }
                for (int ch = 8; ch < 16; ch++) {
                    eegSamples.push_back(static_cast<float>(packet.pib2_Data[ch]) *
                                         PHYSIO_SCALING_9_16);
                }
            }

            streamer_.pushSample(eegSamples);

            // Push impedance data if in impedance mode and current is injecting
            if (config_.impedance && impedanceStreamer_.hasOutlet()) {
                // Check TR byte: bit 2 cleared means injecting current
                bool injectingCurrent = (packet.tr & 0x04) == 0;

                if (injectingCurrent) {
                    // Calculate compliance voltage
                    float refMicroVolts = static_cast<float>(packet.refMonitor) *
                                          details_.scalingFactor;

                    std::vector<float> impedanceSamples;
                    impedanceSamples.reserve(nChannels);

                    for (int ch = 0; ch < nChannels; ch++) {
                        float channelMicroVolts = static_cast<float>(packet.eegData[ch]) *
                                                  details_.scalingFactor;
                        // Compliance voltage formula: (channel + ref) * 201
                        float complianceMicroVolts = (channelMicroVolts + refMicroVolts) * 201.0f;
                        // Convert to volts
                        float complianceVolts = complianceMicroVolts * 1e-6f;
                        impedanceSamples.push_back(complianceVolts);
                    }

                    impedanceStreamer_.pushSample(impedanceSamples);
                }
            }
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
                uint8_t netCodeByte = (headerBytes[26] & 0x78) >> 3;
                details_.netCode = static_cast<NetCode>(netCodeByte);

                int detectedChannels = getChannelCountFromNetCode(details_.netCode);
                if (detectedChannels > 0) {
                    nChannels = detectedChannels;
                }

                emitStatus(std::string("Sensor: ") + netCodeName(details_.netCode) + "\n");
                emitSensor(details_.netCode);
                emitChannelCount(nChannels);

                // Create LSL outlet (no Physio16 support for PacketFormat1)
                std::string streamName = "EGI NetAmp " + std::to_string(header.ampID);
                streamer_.createOutlet(streamName, nChannels, 0,
                                       config_.sampleRate, config_.serverAddress, details_);
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
