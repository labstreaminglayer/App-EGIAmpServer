#include "egiamp/EGIAmpClient.h"
#include "egiamp/Endian.h"

#include <chrono>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace egiamp {

namespace {

// Calculate anti-alias filter delay in seconds based on sample rate
// These values are from EGI's Anti-Alias Filter Alignment app
double getFilterDelaySeconds(int sampleRate, bool fastRecovery) {
    if (fastRecovery) {
        return 0.0;  // Native rate has no FPGA filter delay
    }
    switch (sampleRate) {
        case 250:  return 112.0 / 1000.0;   // 112 msec
        case 500:  return 66.0 / 1000.0;    // 66 msec
        case 1000: return 36.0 / 1000.0;   // 36 msec
        default:   return 0.0;             // Native rates only (2000+) have no delay
    }
}

} // anonymous namespace

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
    // Create and configure the impedance measurement handler
    impedanceMeasurement_ = std::make_unique<ImpedanceMeasurement>(connection_, config_.amplifierId);

    impedanceMeasurement_->setStatusCallback([this](const std::string& msg) {
        emitStatus(msg);
    });

    impedanceMeasurement_->setSampleRate(config_.sampleRate);
    impedanceMeasurement_->setScalingFactor(details_.scalingFactor);
    impedanceMeasurement_->setNetSize(getChannelCountFromNetCode(details_.netCode));

    // Set up the amplifier in impedance measurement state
    if (!impedanceMeasurement_->setupImpedanceState()) {
        throw std::runtime_error("Failed to configure impedance state");
    }

    return true;
}

bool EGIAmpClient::queryAmplifierDetails() {
    try {
        std::string response = connection_.sendCommand(
            "cmd_GetAmpDetails", config_.amplifierId, 0, "0");

        emitStatus("__________________________\n  Amplifier Details\n__________________________\n");

        std::regex token(R"(\((\w+)\s+([^()]+)\))");
        std::smatch match;

        while (std::regex_search(response, match, token)) {
            std::string key = match[1].str();
            std::string value = match[2].str();

            if (key.find("packet_format") != std::string::npos) {
                details_.packetType = static_cast<PacketType>(std::stoi(value));
                emitStatus("    Packet Format: " + value + "\n");

            } else if (key.find("number_of_channels") != std::string::npos) {
                details_.channelCount = std::stoi(value);
                emitStatus("    Channel Count: " + value + "\n");

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
                emitStatus("    Serial Number: " + value + "\n");

            } else if (key.find("system_version") != std::string::npos) {
                details_.firmwareVersion = value;
                emitStatus("    Firmware Version: " + value + "\n");
            }

            response = match.suffix().str();
        }

        emitStatus(std::string("    Amplifier Type: ") + amplifierTypeName(details_.amplifierType) + "\n");
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
    // Native rates: 500, 1000, 2000, 4000, 8000 (no FPGA anti-alias filter, lower latency)
    // Decimated rates: 250, 500, 1000 (FPGA anti-alias filter, higher latency)
    bool useNative = config_.sampleRate > 1000 ||
                     (config_.fastRecovery && config_.sampleRate >= 500);

    if (useNative) {
        if (config_.fastRecovery && config_.sampleRate < 500) {
            emitStatus("Warning: Fast recovery not available at " +
                       std::to_string(config_.sampleRate) + " Hz, using decimated mode.\n");
        }
        connection_.sendCommand("cmd_SetNativeRate", ampId, 0, sampleRate);
        emitStatus("Using native rate (fast recovery, no FPGA filter).\n");
    } else {
        connection_.sendCommand("cmd_SetDecimatedRate", ampId, 0, sampleRate);
        emitStatus("Using decimated rate (FPGA anti-alias filter enabled).\n");
    }

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

    // Stop impedance measurement if running
    if (impedanceMeasurement_) {
        impedanceMeasurement_->stop();
        if (weInitializedAmp_) {
            // Reset amplifier to default state
            impedanceMeasurement_->resetToDefaultState();
        }
        impedanceMeasurement_.reset();
    }
    impedanceModeActive_ = false;

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
    streamLost_ = false;
    ampRestarted_ = false;
    recoveryAttempts_ = 0;
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
        // Detect the current sample rate
        int detectedRate = detectSampleRate();

        // Determine if we need to reinitialize
        bool needsReinit = false;
        if (config_.forceSampleRate && detectedRate > 0) {
            if (detectedRate != config_.sampleRate) {
                // Rate mismatch - must reinitialize
                needsReinit = true;
            } else if (config_.alignTimestamps && !config_.fastRecovery &&
                       (config_.sampleRate == 500 || config_.sampleRate == 1000)) {
                // Rate matches but we need known filter mode for timestamp alignment
                // 500 and 1000 Hz can be either native or decimated, so reinit to ensure decimated
                needsReinit = true;
                emitStatus("Reinitializing to ensure decimated mode for timestamp alignment.\n");
            }
        }

        // Impedance mode requires specific amplifier configuration
        if (config_.impedance) {
            needsReinit = true;
            emitStatus("Impedance mode requested, reinitializing amplifier...\n");
        }

        if (needsReinit) {
            emitStatus("Amplifier running at " + std::to_string(detectedRate) +
                       " Hz, reinitializing for " + std::to_string(config_.sampleRate) + " Hz...\n");
            // Stop, change rate, restart (no power cycle)
            connection_.sendCommand("cmd_Stop", config_.amplifierId, 0, "0");
            std::string rateStr = std::to_string(config_.sampleRate);
            bool useNative = config_.sampleRate > 1000 ||
                             (config_.fastRecovery && config_.sampleRate >= 500);
            if (useNative) {
                connection_.sendCommand("cmd_SetNativeRate", config_.amplifierId, 0, rateStr);
                emitStatus("Using native rate (fast recovery, no FPGA filter).\n");
            } else {
                connection_.sendCommand("cmd_SetDecimatedRate", config_.amplifierId, 0, rateStr);
                emitStatus("Using decimated rate (FPGA anti-alias filter enabled).\n");
            }

            // Configure impedance state if requested
            if (config_.impedance) {
                try {
                    cmd_ImpedanceAcquisitionState();
                } catch (const std::exception& ex) {
                    emitError(std::string("Failed to configure impedance mode: ") + ex.what());
                    return false;
                }
            }

            connection_.sendCommand("cmd_Start", config_.amplifierId, 0, "0");
            weInitializedAmp_ = true;
        } else {
            emitStatus("Amplifier is already running, skipping initialization.\n");
            weInitializedAmp_ = false;

            // Use detected sample rate if available
            if (detectedRate > 0) {
                config_.sampleRate = detectedRate;
                emitStatus("Using detected sample rate: " + std::to_string(detectedRate) + " Hz\n");
            }
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

    // Read notifications until we find physio status or timeout.
    // During cold start, several notifications may precede the physio response
    // (client_connected, ntn_AmpPowerOff, ntn_AmpPowerOn, ntn_AmpStarted, etc.)
    // and some arrive with delay as the amplifier initializes hardware.
    physioConnectionStatus_ = 0;
    try {
        auto& notifStream = connection_.notificationStream();
        std::regex statusRegex(R"(ntn_PhysioConnectionStatus\s+\d+\s+\d+\s+\+(\d+))");

        for (int attempt = 0; attempt < 10; attempt++) {
            notifStream.clear();  // Reset stream state after any prior timeout
            connection_.setNotificationStreamTimeout(std::chrono::milliseconds(500));
            char notifBuffer[4096];
            notifStream.getline(notifBuffer, sizeof(notifBuffer));

            if (!notifStream.good()) {
                continue;  // Timeout or read error, retry
            }

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
    recoveryCv_.notify_all();  // Wake reader thread if blocked on recovery

    if (readerThread_) {
        readerThread_->join();
        readerThread_.reset();
    }

    if (notificationThread_) {
        notificationThread_->join();
        notificationThread_.reset();
    }
}

bool EGIAmpClient::startImpedanceMode() {
    if (!isStreaming()) {
        emitError("Cannot start impedance mode: not streaming");
        return false;
    }

    if (impedanceModeActive_) {
        return true;  // Already active
    }

    emitStatus("Starting impedance mode...\n");

    // Create and configure impedance measurement handler
    impedanceMeasurement_ = std::make_unique<ImpedanceMeasurement>(connection_, config_.amplifierId);
    impedanceMeasurement_->setStatusCallback([this](const std::string& msg) {
        emitStatus(msg);
    });
    impedanceMeasurement_->setSampleRate(config_.sampleRate);
    impedanceMeasurement_->setScalingFactor(details_.scalingFactor);
    impedanceMeasurement_->setNetSize(getChannelCountFromNetCode(details_.netCode));

    // Set up the amplifier in impedance measurement state
    if (!impedanceMeasurement_->setupImpedanceState()) {
        emitError("Failed to configure impedance state");
        impedanceMeasurement_.reset();
        return false;
    }

    // Create impedance LSL outlet
    int nChannels = getChannelCountFromNetCode(details_.netCode);
    if (nChannels <= 0) {
        nChannels = details_.channelCount;
    }
    std::string streamName = config_.streamName() + "_Impedance";
    impedanceStreamer_.createImpedanceOutlet(streamName, nChannels, config_.serverAddress, details_);
    emitStatus("Impedance stream created.\n");

    // Configure channel count and start scanning
    impedanceMeasurement_->setChannelCount(nChannels);
    impedanceMeasurement_->startContinuousScan(impedanceStreamer_);
    emitStatus("Impedance scanning started.\n");

    impedanceModeActive_ = true;
    return true;
}

bool EGIAmpClient::stopImpedanceMode() {
    if (!impedanceModeActive_) {
        return true;  // Already stopped
    }

    emitStatus("Stopping impedance mode...\n");

    // Stop scanning and reset amplifier state
    if (impedanceMeasurement_) {
        impedanceMeasurement_->stop();
        impedanceMeasurement_->resetToDefaultState();
        impedanceMeasurement_.reset();
    }

    // Close impedance outlet
    impedanceStreamer_.closeOutlet();
    emitStatus("Impedance stream closed.\n");

    impedanceModeActive_ = false;
    emitStatus("Impedance mode stopped.\n");
    return true;
}

bool EGIAmpClient::isImpedanceModeActive() const {
    return impedanceModeActive_;
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
    bool ampStopped = false;
    bool pendingRecovery = false;  // Set when stop+poweroff sequence detected

    while (!stopFlag_) {
        // If stop+poweroff was detected and reader has noticed stream loss,
        // proactively reinitialize the amp with our original settings
        if (pendingRecovery && streamLost_) {
            pendingRecovery = false;
            ampStopped = false;
            attemptRecovery(true);
            continue;
        }

        stream.clear();  // Reset stream state after any prior timeout
        connection_.setNotificationStreamTimeout(std::chrono::seconds(1));
        char response[4096];
        stream.getline(response, sizeof(response));

        if (!stream.good()) {
            continue;  // Timeout, retry
        }

        std::string notification(response);
        if (notification.length() > 0) {
            emitStatus("__________________________\n  Notification Received\n    " +
                       notification + "\n__________________________\n");

            if (notification.find("ntn_AmpStopped") != std::string::npos) {
                ampStopped = true;
            } else if (notification.find("ntn_AmpPowerOff") != std::string::npos) {
                if (ampStopped) {
                    pendingRecovery = true;
                }
            } else if (notification.find("ntn_AmpStarted") != std::string::npos) {
                if (pendingRecovery && streamLost_) {
                    // Stop+poweroff was pending but amp restarted externally before
                    // we could reinitialize — use passive recovery instead
                    pendingRecovery = false;
                    ampStopped = false;
                    attemptRecovery(false);
                } else if (streamLost_) {
                    // Stream died, amp restarted without power cycle — passive recovery
                    ampStopped = false;
                    attemptRecovery(false);
                } else {
                    // Normal restart while stream is alive (e.g. sample rate change)
                    ampStopped = false;
                    pendingRecovery = false;
                    sampleRateChangeDetected_ = true;
                }
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

    while (!stopFlag_) {
        // Inner read loop — reads packets until stream dies or stopFlag
        while (stream.good() && !stopFlag_) {
            AmpDataPacketHeader header;
            stream.clear();
            connection_.setDataStreamTimeout(std::chrono::seconds(5));
            stream.read(reinterpret_cast<char*>(&header), sizeof(header));

            // Capture arrival time before any per-sample processing so that
            // all samples in this batch share the same base timestamp.
            double batchTimestamp = lsl::local_clock();

            header.ampID = big_to_native(header.ampID);
            header.length = big_to_native(header.length);

            int nSamples = header.length / sizeof(PacketFormat2);
            int uniquePackets = 0;

            // Accumulate samples for chunk push
            std::vector<std::vector<int32_t>> chunkInt32;
            std::vector<std::vector<float>> chunkFloat;

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
                    // (queried in startStreaming() or reQueryPhysioStatus())
                    int physioChannelCount = 0;
                    if (physioConnectionStatus_ == 1 || physioConnectionStatus_ == 2) {
                        physioChannelCount = 16;
                    } else if (physioConnectionStatus_ == 3) {
                        physioChannelCount = 32;
                    }

                    constexpr int dinChannelCount = 1;  // Single channel with raw 16-bit value
                    emitChannelCount(nChannels + physioChannelCount + dinChannelCount);

                    // Create LSL outlet for EEG (+ Physio if connected + DIN)
                    std::string streamName = config_.streamName();
                    streamer_.createOutlet(streamName, nChannels, physioChannelCount, dinChannelCount,
                                           config_.sampleRate, config_.serverAddress, details_,
                                           config_.nativeFormat);

                    // Apply timestamp offset for filter delay compensation if enabled
                    if (config_.alignTimestamps) {
                        double delaySeconds = getFilterDelaySeconds(config_.sampleRate, config_.fastRecovery);
                        streamer_.setTimestampOffset(delaySeconds);
                        if (delaySeconds > 0) {
                            emitStatus("Timestamp alignment enabled: " +
                                       std::to_string(static_cast<int>(delaySeconds * 1000)) + " ms offset.\n");
                        }
                        if (ampRestarted_ && !weInitializedAmp_) {
                            emitStatus("Warning: After passive recovery, timestamp alignment may be inaccurate "
                                       "(unknown if external app used native or decimated mode).\n");
                        }
                    }

                    // Create LSL outlet for impedance if enabled and start scanning
                    if (config_.impedance && impedanceMeasurement_) {
                        std::string impedanceStreamName = streamName + "_Impedance";
                        impedanceStreamer_.createImpedanceOutlet(impedanceStreamName, nChannels,
                                                                 config_.serverAddress, details_);
                        emitStatus("Impedance stream created.\n");

                        // Configure and start the impedance measurement
                        impedanceMeasurement_->setChannelCount(nChannels);
                        impedanceMeasurement_->startContinuousScan(impedanceStreamer_);
                        emitStatus("Impedance scanning started.\n");
                        impedanceModeActive_ = true;
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

                            // Snap to standard rates (250, 500, 1000, 2000, 4000, 8000)
                            int newRate;
                            if (measuredRate < 375) newRate = 250;
                            else if (measuredRate < 750) newRate = 500;
                            else if (measuredRate < 1500) newRate = 1000;
                            else if (measuredRate < 3000) newRate = 2000;
                            else if (measuredRate < 6000) newRate = 4000;
                            else newRate = 8000;

                            if (newRate != config_.sampleRate) {
                                emitStatus("Sample rate changed from " +
                                           std::to_string(config_.sampleRate) + " Hz to " +
                                           std::to_string(newRate) + " Hz, recreating LSL outlets...\n");
                                config_.sampleRate = newRate;

                                // Recreate LSL outlets with new rate
                                streamer_.closeOutlet();
                                std::string streamName = config_.streamName();
                                int physioChCount = (physioConnectionStatus_ == 3) ? 32 :
                                                    (physioConnectionStatus_ > 0) ? 16 : 0;
                                constexpr int dinChCount = 1;  // Single channel with raw 16-bit value
                                streamer_.createOutlet(streamName, nChannels, physioChCount, dinChCount,
                                                       config_.sampleRate, config_.serverAddress, details_,
                                                       config_.nativeFormat);

                                // Apply timestamp offset for filter delay compensation if enabled
                                if (config_.alignTimestamps) {
                                    double delaySeconds = getFilterDelaySeconds(config_.sampleRate, config_.fastRecovery);
                                    streamer_.setTimestampOffset(delaySeconds);
                                }

                                // Recreate impedance outlet and restart scanning if active
                                if (impedanceModeActive_ && impedanceMeasurement_) {
                                    impedanceMeasurement_->stop();
                                    impedanceStreamer_.closeOutlet();
                                    std::string impedanceStreamName = streamName + "_Impedance";
                                    impedanceStreamer_.createImpedanceOutlet(impedanceStreamName, nChannels,
                                                                             config_.serverAddress, details_);
                                    impedanceMeasurement_->setSampleRate(config_.sampleRate);
                                    impedanceMeasurement_->setNetSize(getChannelCountFromNetCode(details_.netCode));
                                    impedanceMeasurement_->startContinuousScan(impedanceStreamer_);
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

                // Push sample to EEG stream (PacketFormat2 is little endian natively)
                int physioChannels = (physioConnectionStatus_ == 3) ? 32 :
                                     (physioConnectionStatus_ > 0) ? 16 : 0;

                if (config_.nativeFormat) {
                    // Native format: push raw int32 ADC counts
                    std::vector<int32_t> rawSamples;
                    rawSamples.reserve(nChannels + physioChannels + 1);

                    for (int ch = 0; ch < nChannels; ch++) {
                        rawSamples.push_back(packet.eegData[ch]);
                    }

                    // Add PIB1 channels (if port 1 connected: status 1 or 3)
                    if (physioConnectionStatus_ & 0x01) {
                        for (int ch = 0; ch < 16; ch++) {
                            rawSamples.push_back(packet.pib1_Data[ch]);
                        }
                    }

                    // Add PIB2 channels (if port 2 connected: status 2 or 3)
                    if (physioConnectionStatus_ & 0x02) {
                        for (int ch = 0; ch < 16; ch++) {
                            rawSamples.push_back(packet.pib2_Data[ch]);
                        }
                    }

                    // Add DIN channel (raw 16-bit value)
                    rawSamples.push_back(static_cast<int32_t>(packet.digitalInputs));

                    chunkInt32.push_back(std::move(rawSamples));
                } else {
                    // Default: convert to float microvolts
                    std::vector<float> eegSamples;
                    eegSamples.reserve(nChannels + physioChannels + 1);

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

                    // Add DIN channel (raw 16-bit value)
                    eegSamples.push_back(static_cast<float>(packet.digitalInputs));

                    chunkFloat.push_back(std::move(eegSamples));
                }

                // Feed samples to impedance measurement if impedance mode is active
                if (impedanceModeActive_ && impedanceMeasurement_) {
                    impedanceMeasurement_->feedSample(packet);
                }
            }

            // Push accumulated chunk with batch arrival timestamp
            if (!chunkInt32.empty()) {
                streamer_.pushChunkInt32(chunkInt32, batchTimestamp);
            }
            if (!chunkFloat.empty()) {
                streamer_.pushChunk(chunkFloat, batchTimestamp);
            }
        }

        // Inner loop exited — stream lost or stopFlag
        if (stopFlag_) {
            break;
        }

        // Stream lost — wait for notification thread to recover
        emitError("The stream was lost. Waiting for amplifier restart...\n");
        streamLost_ = true;

        {
            std::unique_lock<std::mutex> lock(recoveryMutex_);
            bool recovered = recoveryCv_.wait_for(lock, std::chrono::seconds(120), [this] {
                return ampRestarted_.load() || stopFlag_.load();
            });

            if (!recovered || stopFlag_) {
                emitError("Recovery timed out. Stopping.");
                break;
            }
        }

        // Recovery succeeded — reset local state for re-entry into inner loop
        emitStatus("Stream recovered. Resuming data reading.\n");
        lastPacketCounter = 0;
        firstPacketReceived = false;
        measuringNewRate = false;
        rateCheckSampleCount = 0;
        lastTimeStamp_ = 0;
        lastPacketCounterWithTimeStamp_ = 0;
        ampRestarted_ = false;
        nChannels = details_.channelCount;

        // attemptRecovery() reconnected the data stream and sent cmd_ListenToAmp.
        // If settings changed, the outlet was closed and !streamer_.hasOutlet()
        // will trigger recreation on first packet. Otherwise we reuse the outlet.
        stream.clear();
    }
}

bool EGIAmpClient::attemptRecovery(bool reinitialize) {
    int attempt = ++recoveryAttempts_;
    if (attempt > 5) {
        emitError("Maximum recovery attempts (5) exceeded. Giving up.");
        stopFlag_ = true;
        recoveryCv_.notify_all();
        return false;
    }

    emitStatus("Attempting stream recovery (attempt " + std::to_string(attempt) + "/5)...\n");

    try {
        int previousRate = config_.sampleRate;
        int previousPhysioStatus = physioConnectionStatus_;

        if (reinitialize) {
            // Proactive recovery: reinitialize the amp with our original settings.
            // initAmplifier() uses only the command stream, which is safe while the
            // reader thread is still trying to read from the data stream.
            emitStatus("Reinitializing amplifier with original settings...\n");
            initAmplifier();
            weInitializedAmp_ = true;

            // Wait for reader thread to detect stream loss before touching data stream.
            // The reader has a 5-second timeout, so this resolves quickly.
            for (int i = 0; i < 100 && !streamLost_ && !stopFlag_; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (stopFlag_) return false;
            if (!streamLost_) {
                emitError("Reader thread did not detect stream loss within timeout.");
                return false;
            }
        }

        // At this point, streamLost_ is true and reader thread is blocked on CV.
        // Safe to touch the data stream.

        // Reconnect data stream
        if (!connection_.reconnectDataStream()) {
            emitError("Failed to reconnect data stream during recovery.");
            return false;
        }

        bool needsNewOutlet = false;

        if (!reinitialize) {
            // Passive recovery: detect the sample rate set by external app
            int newRate = detectSampleRate();
            if (newRate > 0) {
                config_.sampleRate = newRate;
                if (newRate != previousRate) {
                    emitStatus("Sample rate changed from " + std::to_string(previousRate) +
                               " Hz to " + std::to_string(newRate) + " Hz during recovery.\n");
                    needsNewOutlet = true;
                }
            } else {
                emitStatus("Warning: Could not detect sample rate after recovery, keeping " +
                           std::to_string(config_.sampleRate) + " Hz\n");
            }

            // Even when the rate matches, the mode (native vs decimated) might differ.
            // Close the outlet when we can't confirm the mode is the same.
            if (!needsNewOutlet && streamer_.hasOutlet()) {
                bool weUseNative = config_.sampleRate > 1000 ||
                                   (config_.fastRecovery && config_.sampleRate >= 500);
                if (config_.sampleRate == 1000) {
                    // At 1000 Hz, there is no way to distinguish native from decimated
                    // in the data stream. Close to be safe.
                    needsNewOutlet = true;
                    emitStatus("Recreating outlet: cannot confirm mode match at 1000 Hz.\n");
                } else if (config_.sampleRate == 500 && weUseNative) {
                    // Net Station always uses decimated 500 Hz, but we use native.
                    needsNewOutlet = true;
                    emitStatus("Recreating outlet: our native 500 Hz vs external decimated 500 Hz.\n");
                }
                // 250 Hz: only decimated mode exists — always matches
                // 500 Hz decimated: Net Station also uses decimated — matches
                // 2000+ Hz: only native mode exists — always matches
            }

            weInitializedAmp_ = false;
        }
        // When reinitializing, settings are guaranteed to match — keep outlet

        // Re-query Physio16 status (also consumes any queued notifications
        // from initAmplifier() while looking for the physio response)
        reQueryPhysioStatus();

        // If physio connection changed, channel count in the outlet is wrong
        if (physioConnectionStatus_ != previousPhysioStatus) {
            needsNewOutlet = true;
            emitStatus("Physio16 connection changed, recreating outlet.\n");
        }

        // Disable impedance mode if it was active (restart changes amp config)
        if (impedanceModeActive_) {
            emitStatus("Disabling impedance mode after recovery (amp restart changes configuration).\n");
            if (impedanceMeasurement_) {
                impedanceMeasurement_->stop();
                impedanceMeasurement_.reset();
            }
            impedanceStreamer_.closeOutlet();
            impedanceModeActive_ = false;
        }

        if (needsNewOutlet) {
            streamer_.closeOutlet();
        } else if (streamer_.hasOutlet()) {
            emitStatus("Reusing existing LSL outlet (settings unchanged).\n");
        }

        // Start listening for data on the new stream
        connection_.sendDatastreamCommand("cmd_ListenToAmp", config_.amplifierId, 0, "0");

        // Signal recovery to reader thread
        streamLost_ = false;
        ampRestarted_ = true;
        recoveryAttempts_ = 0;
        recoveryCv_.notify_all();

        emitStatus("Stream recovery successful.\n");
        return true;

    } catch (const std::exception& ex) {
        emitError(std::string("Recovery attempt failed: ") + ex.what());
        return false;
    } catch (...) {
        emitError("Recovery attempt failed with unknown error.");
        return false;
    }
}

void EGIAmpClient::reQueryPhysioStatus() {
    try {
        connection_.sendCommand("cmd_GetPhysioConnectionStatus", config_.amplifierId, 0, "0");

        auto& notifStream = connection_.notificationStream();
        std::regex statusRegex(R"(ntn_PhysioConnectionStatus\s+\d+\s+\d+\s+\+(\d+))");

        for (int attempt = 0; attempt < 10; attempt++) {
            notifStream.clear();
            connection_.setNotificationStreamTimeout(std::chrono::milliseconds(500));
            char notifBuffer[4096];
            notifStream.getline(notifBuffer, sizeof(notifBuffer));

            if (!notifStream.good()) {
                continue;
            }

            std::string notification(notifBuffer);
            std::smatch match;
            if (std::regex_search(notification, match, statusRegex)) {
                physioConnectionStatus_ = std::stoi(match[1].str());
                emitStatus("Physio16 connection status after recovery: " +
                           std::to_string(physioConnectionStatus_) + "\n");
                return;
            }
            // Log non-matching notifications consumed during physio query
            if (!notification.empty()) {
                emitStatus("(during physio re-query) " + notification + "\n");
            }
        }

        emitStatus("Physio16: query timed out during recovery, keeping previous status\n");
    } catch (...) {
        emitStatus("Physio16: query failed during recovery, keeping previous status\n");
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
        double batchTimestamp = lsl::local_clock();

        header.ampID = big_to_native(header.ampID);
        header.length = big_to_native(header.length);

        int nSamples = header.length / sizeof(PacketFormat1);
        std::vector<std::vector<float>> chunk;

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

                // Create LSL outlet (no Physio16 or DIN support for PacketFormat1)
                // Note: PacketFormat1 (NA300) already provides float data, so nativeFormat
                // doesn't apply - we always use float for Format1
                std::string streamName = config_.streamName();
                streamer_.createOutlet(streamName, nChannels, 0, 0,
                                       config_.sampleRate, config_.serverAddress, details_,
                                       false);  // Format1 is always float
            }

            // Convert endianness and accumulate sample
            std::vector<float> sample;
            sample.reserve(nChannels);
            for (int i = 0; i < nChannels; i++) {
                float val = packet.eeg[i];
                big_to_native_inplace(val);
                sample.push_back(val);
            }
            chunk.push_back(std::move(sample));
        }

        if (!chunk.empty()) {
            streamer_.pushChunk(chunk, batchTimestamp);
        }
    }

    if (!stream.good() && !stopFlag_) {
        emitError("The stream was lost.");
    }
}

} // namespace egiamp
