#include "CommandHandler.h"
#include "NotificationHandler.h"
#include <iostream>
#include <regex>
#include <sstream>

namespace mock {

CommandHandler::CommandHandler(asio::io_context& io_context, uint16_t port,
                               std::shared_ptr<MockAmplifier> amplifier,
                               std::shared_ptr<NotificationHandler> notificationHandler)
    : io_context_(io_context)
    , acceptor_(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
    , amplifier_(amplifier)
    , notificationHandler_(notificationHandler)
{
    initCommandMap();
}

CommandHandler::~CommandHandler() {
    stop();
}

void CommandHandler::initCommandMap() {
    // Amplifier commands
    commandMap_["cmd_None"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdNone(a, c, v); };
    commandMap_["cmd_Start"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdStart(a, c, v); };
    commandMap_["cmd_Stop"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdStop(a, c, v); };
    commandMap_["cmd_SetPower"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdSetPower(a, c, v); };
    commandMap_["cmd_Reset"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdReset(a, c, v); };

    // 10K Ohm commands
    commandMap_["cmd_TurnAll10KOhms"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdTurnAll10KOhms(a, c, v); };
    commandMap_["cmd_TurnChannel10KOhms"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdTurnChannel10KOhms(a, c, v); };
    commandMap_["cmd_SetCOM10KOhms"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdSetCOM10KOhms(a, c, v); };
    commandMap_["cmd_SetReference10KOhms"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdSetReference10KOhms(a, c, v); };

    // Drive signal commands
    commandMap_["cmd_TurnAllDriveSignals"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdTurnAllDriveSignals(a, c, v); };
    commandMap_["cmd_TurnChannelDriveSignals"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdTurnChannelDriveSignals(a, c, v); };
    commandMap_["cmd_SetCOMDriveSignal"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdSetCOMDriveSignal(a, c, v); };
    commandMap_["cmd_SetReferenceDriveSignal"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdSetReferenceDriveSignal(a, c, v); };

    // Configuration commands
    commandMap_["cmd_SetSubjectGround"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdSetSubjectGround(a, c, v); };
    commandMap_["cmd_SetCurrentSource"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdSetCurrentSource(a, c, v); };
    commandMap_["cmd_SetCalibrationSignalFreq"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdSetCalibrationSignalFreq(a, c, v); };
    commandMap_["cmd_SetBufferedReference"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdSetBufferedReference(a, c, v); };
    commandMap_["cmd_SetOscillatorGate"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdSetOscillatorGate(a, c, v); };
    commandMap_["cmd_SetWaveShape"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdSetWaveShape(a, c, v); };
    commandMap_["cmd_SetDrivenCommon"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdSetDrivenCommon(a, c, v); };
    commandMap_["cmd_SetCalibrationSignalAmplitude"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdSetCalibrationSignalAmplitude(a, c, v); };

    // Digital I/O
    commandMap_["cmd_SetDigitalOutputData"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdSetDigitalOutputData(a, c, v); };
    commandMap_["cmd_SetDigitalInOutDirection"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdSetDigitalInOutDirection(a, c, v); };

    // Sample rate
    commandMap_["cmd_SetNativeRate"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdSetNativeRate(a, c, v); };
    commandMap_["cmd_SetDecimatedRate"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdSetDecimatedRate(a, c, v); };

    // Query commands
    commandMap_["cmd_GetStartTime"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdGetStartTime(a, c, v); };
    commandMap_["cmd_GetAmpDetails"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdGetAmpDetails(a, c, v); };
    commandMap_["cmd_GetAmpStatus"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdGetAmpStatus(a, c, v); };

    // Zero ohms
    commandMap_["cmd_TurnChannelZeroOhms"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdTurnChannelZeroOhms(a, c, v); };
    commandMap_["cmd_TurnAllZeroOhms"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdTurnAllZeroOhms(a, c, v); };

    // PIB/Physio
    commandMap_["cmd_setPIBChannelGain"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdSetPIBChannelGain(a, c, v); };
    commandMap_["cmd_GetPhysioConnectionStatus"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdGetPhysioConnectionStatus(a, c, v); };

    // Amp Server commands
    commandMap_["cmd_NumberOfAmps"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdNumberOfAmps(a, c, v); };
    commandMap_["cmd_NumberOfActiveAmps"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdNumberOfActiveAmps(a, c, v); };
    commandMap_["cmd_ListenToAmp"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdListenToAmp(a, c, v); };
    commandMap_["cmd_StopListeningToAmp"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdStopListeningToAmp(a, c, v); };
    commandMap_["cmd_ReceiveNotifications"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdReceiveNotifications(a, c, v); };
    commandMap_["cmd_StopReceivingNotifications"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdStopReceivingNotifications(a, c, v); };
    commandMap_["cmd_Exit"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdExit(a, c, v); };

    // Extended commands
    commandMap_["cmd_DefaultAcquisitionState"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdDefaultAcquisitionState(a, c, v); };
    commandMap_["cmd_DefaultSignalGeneration"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdDefaultSignalGeneration(a, c, v); };

    // GTEN commands
    commandMap_["cmd_GTENSetTrain"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdGTENSetTrain(a, c, v); };
    commandMap_["cmd_GTENSetWaveforms"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdGTENSetWaveforms(a, c, v); };
    commandMap_["cmd_GTENSetBlocks"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdGTENSetBlocks(a, c, v); };
    commandMap_["cmd_GTENStartTrain"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdGTENStartTrain(a, c, v); };
    commandMap_["cmd_GTENAbortTrain"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdGTENAbortTrain(a, c, v); };
    commandMap_["cmd_GTENGetStatus"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdGTENGetStatus(a, c, v); };
    commandMap_["cmd_GTENResetAlarm"] = [](CommandHandler* h, int64_t a, int16_t c, const std::string& v) { return h->cmdGTENResetAlarm(a, c, v); };
}

void CommandHandler::start() {
    running_ = true;
    acceptConnections();
    std::cout << "[CommandHandler] Listening on port " << acceptor_.local_endpoint().port() << std::endl;
}

void CommandHandler::stop() {
    running_ = false;
    asio::error_code ec;
    acceptor_.close(ec);
}

void CommandHandler::acceptConnections() {
    auto socket = std::make_shared<asio::ip::tcp::socket>(io_context_);
    acceptor_.async_accept(*socket, [this, socket](const asio::error_code& ec) {
        if (!ec && running_) {
            std::cout << "[CommandHandler] Client connected" << std::endl;
            handleConnection(socket);
        }
        if (running_) {
            acceptConnections();
        }
    });
}

void CommandHandler::handleConnection(std::shared_ptr<asio::ip::tcp::socket> socket) {
    readCommand(socket);
}

void CommandHandler::readCommand(std::shared_ptr<asio::ip::tcp::socket> socket) {
    auto buffer = std::make_shared<asio::streambuf>();
    asio::async_read_until(*socket, *buffer, '\n',
        [this, socket, buffer](const asio::error_code& ec, std::size_t bytes_transferred) {
            if (!ec) {
                std::istream is(buffer.get());
                std::string line;
                std::getline(is, line);

                // Remove trailing carriage return if present
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                std::cout << "[CommandHandler] Received: " << line << std::endl;

                std::string response = processCommand(line);
                std::cout << "[CommandHandler] Response: " << response << std::endl;

                // Send response
                auto responseData = std::make_shared<std::string>(response + "\n");
                asio::async_write(*socket, asio::buffer(*responseData),
                    [this, socket, responseData](const asio::error_code& ec, std::size_t) {
                        if (!ec && running_ && !exitRequested_) {
                            readCommand(socket);
                        }
                    });
            }
        });
}

std::string CommandHandler::processCommand(const std::string& command) {
    return parseAndExecuteCommand(command);
}

std::string CommandHandler::parseAndExecuteCommand(const std::string& cmdLine) {
    // Parse: (sendCommand cmd_Name ampId channel value)
    // The value can contain spaces (e.g., XML data)

    std::regex cmdRegex(R"(\(sendCommand\s+(\S+)\s+(-?\d+)\s+(-?\d+)\s*(.*)\))");
    std::smatch match;

    if (std::regex_match(cmdLine, match, cmdRegex)) {
        std::string cmdName = match[1].str();
        int64_t ampId = std::stoll(match[2].str());
        int16_t channel = static_cast<int16_t>(std::stoi(match[3].str()));
        std::string value = match[4].str();

        auto it = commandMap_.find(cmdName);
        if (it != commandMap_.end()) {
            return it->second(this, ampId, channel, value);
        } else {
            std::cerr << "[CommandHandler] Unknown command: " << cmdName << std::endl;
            return errorResponse();
        }
    }

    return errorResponse();
}

std::string CommandHandler::successResponse() {
    return "(sendCommand_return (status complete))";
}

std::string CommandHandler::errorResponse() {
    return "(sendCommand_return (status error))";
}

std::string CommandHandler::responseWithData(const std::string& data) {
    return "(sendCommand_return (status complete) " + data + ")";
}

void CommandHandler::sendNotification(const std::string& notification) {
    if (notificationHandler_) {
        notificationHandler_->sendNotification(notification);
    }
}

// Command implementations
std::string CommandHandler::cmdNone(int64_t ampId, int16_t channel, const std::string& value) {
    return successResponse();
}

std::string CommandHandler::cmdStart(int64_t ampId, int16_t channel, const std::string& value) {
    if (amplifier_->start()) {
        return successResponse();
    }
    return errorResponse();
}

std::string CommandHandler::cmdStop(int64_t ampId, int16_t channel, const std::string& value) {
    amplifier_->stop();
    return successResponse();
}

std::string CommandHandler::cmdSetPower(int64_t ampId, int16_t channel, const std::string& value) {
    bool on = (value == "1" || value.empty());
    amplifier_->setPower(on);
    return successResponse();
}

std::string CommandHandler::cmdReset(int64_t ampId, int16_t channel, const std::string& value) {
    amplifier_->reset();
    return successResponse();
}

std::string CommandHandler::cmdTurnAll10KOhms(int64_t ampId, int16_t channel, const std::string& value) {
    bool on = (value == "1");
    amplifier_->setAll10KOhms(on);
    return successResponse();
}

std::string CommandHandler::cmdTurnChannel10KOhms(int64_t ampId, int16_t channel, const std::string& value) {
    bool on = (value == "1");
    amplifier_->setChannel10KOhms(channel, on);
    return successResponse();
}

std::string CommandHandler::cmdSetCOM10KOhms(int64_t ampId, int16_t channel, const std::string& value) {
    amplifier_->setCom10KOhms(true);
    return successResponse();
}

std::string CommandHandler::cmdSetReference10KOhms(int64_t ampId, int16_t channel, const std::string& value) {
    bool on = !value.empty() && value != "0";
    amplifier_->setReference10KOhms(on);
    return successResponse();
}

std::string CommandHandler::cmdTurnAllDriveSignals(int64_t ampId, int16_t channel, const std::string& value) {
    bool on = (value == "1");
    amplifier_->setAllDriveSignals(on);
    return successResponse();
}

std::string CommandHandler::cmdTurnChannelDriveSignals(int64_t ampId, int16_t channel, const std::string& value) {
    bool on = (value == "1");
    amplifier_->setChannelDriveSignals(channel, on);
    return successResponse();
}

std::string CommandHandler::cmdSetCOMDriveSignal(int64_t ampId, int16_t channel, const std::string& value) {
    amplifier_->setComDriveSignal(true);
    return successResponse();
}

std::string CommandHandler::cmdSetReferenceDriveSignal(int64_t ampId, int16_t channel, const std::string& value) {
    bool on = !value.empty() && value != "0";
    amplifier_->setReferenceDriveSignal(on);
    return successResponse();
}

std::string CommandHandler::cmdSetSubjectGround(int64_t ampId, int16_t channel, const std::string& value) {
    bool on = (value == "1");
    amplifier_->setSubjectGround(on);
    return successResponse();
}

std::string CommandHandler::cmdSetCurrentSource(int64_t ampId, int16_t channel, const std::string& value) {
    int val = value.empty() ? 0 : std::stoi(value);
    amplifier_->setCurrentSource(val);
    return successResponse();
}

std::string CommandHandler::cmdSetCalibrationSignalFreq(int64_t ampId, int16_t channel, const std::string& value) {
    int freq = value.empty() ? 0 : std::stoi(value);
    amplifier_->setCalibrationSignalFreq(freq);
    return successResponse();
}

std::string CommandHandler::cmdSetBufferedReference(int64_t ampId, int16_t channel, const std::string& value) {
    bool on = (value == "0");  // 0 = On, 1 = Off per spec
    amplifier_->setBufferedReference(on);
    return successResponse();
}

std::string CommandHandler::cmdSetOscillatorGate(int64_t ampId, int16_t channel, const std::string& value) {
    bool on = !value.empty() && value != "0";
    amplifier_->setOscillatorGate(on);
    return successResponse();
}

std::string CommandHandler::cmdSetWaveShape(int64_t ampId, int16_t channel, const std::string& value) {
    int shape = value.empty() ? 0 : std::stoi(value);
    amplifier_->setWaveShape(static_cast<WaveShape>(shape));
    return successResponse();
}

std::string CommandHandler::cmdSetDrivenCommon(int64_t ampId, int16_t channel, const std::string& value) {
    bool on = (value == "1");
    amplifier_->setDrivenCommon(on);
    return successResponse();
}

std::string CommandHandler::cmdSetCalibrationSignalAmplitude(int64_t ampId, int16_t channel, const std::string& value) {
    int amplitude = value.empty() ? 0 : std::stoi(value);
    amplifier_->setCalibrationSignalAmplitude(amplitude);
    return successResponse();
}

std::string CommandHandler::cmdSetDigitalOutputData(int64_t ampId, int16_t channel, const std::string& value) {
    uint16_t data = value.empty() ? 0 : static_cast<uint16_t>(std::stoi(value));
    amplifier_->setDigitalOutputData(data);
    return successResponse();
}

std::string CommandHandler::cmdSetDigitalInOutDirection(int64_t ampId, int16_t channel, const std::string& value) {
    uint16_t direction = value.empty() ? 0 : static_cast<uint16_t>(std::stoi(value));
    amplifier_->setDigitalInOutDirection(direction);
    return successResponse();
}

std::string CommandHandler::cmdSetNativeRate(int64_t ampId, int16_t channel, const std::string& value) {
    int rate = value.empty() ? 1000 : std::stoi(value);
    if (amplifier_->setNativeRate(rate)) {
        return successResponse();
    }
    return errorResponse();
}

std::string CommandHandler::cmdSetDecimatedRate(int64_t ampId, int16_t channel, const std::string& value) {
    int rate = value.empty() ? 1000 : std::stoi(value);
    if (amplifier_->setDecimatedRate(rate)) {
        return successResponse();
    }
    return errorResponse();
}

std::string CommandHandler::cmdGetStartTime(int64_t ampId, int16_t channel, const std::string& value) {
    int64_t startTime = amplifier_->getStartTime();
    return responseWithData("(amp_status_info (start_time " + std::to_string(startTime) + "))");
}

std::string CommandHandler::cmdGetAmpDetails(int64_t ampId, int16_t channel, const std::string& value) {
    const auto& state = amplifier_->state();
    std::ostringstream oss;
    oss << "(amp_details "
        << "(serial_number " << state.serialNumber << ") "
        << "(amp_type " << amplifierTypeName(state.amplifierType) << ") "
        << "(legacy_board " << (state.legacyBoard ? "true" : "false") << ") "
        << "(packet_format " << static_cast<int>(state.packetFormat) << ") "
        << "(system_version " << state.firmwareVersion << ") "
        << "(number_of_channels " << state.channelCount << "))";
    return responseWithData(oss.str());
}

std::string CommandHandler::cmdGetAmpStatus(int64_t ampId, int16_t channel, const std::string& value) {
    // Real amp just returns success with no data
    return successResponse();
}

std::string CommandHandler::cmdTurnChannelZeroOhms(int64_t ampId, int16_t channel, const std::string& value) {
    bool ground = (value == "1");
    amplifier_->setChannelZeroOhms(channel, ground);
    return successResponse();
}

std::string CommandHandler::cmdTurnAllZeroOhms(int64_t ampId, int16_t channel, const std::string& value) {
    bool ground = (value == "1");
    amplifier_->setAllZeroOhms(ground);
    return successResponse();
}

std::string CommandHandler::cmdSetPIBChannelGain(int64_t ampId, int16_t channel, const std::string& value) {
    int gain = value.empty() ? 1 : std::stoi(value);
    amplifier_->setPIBChannelGain(channel, gain);
    return successResponse();
}

std::string CommandHandler::cmdGetPhysioConnectionStatus(int64_t ampId, int16_t channel, const std::string& value) {
    int status = amplifier_->getPhysioConnectionStatus();
    if (notificationHandler_) {
        notificationHandler_->sendPhysioConnectionStatus(status);
    }
    // Include status in response for synchronous access
    return responseWithData("(physio_connection_status " + std::to_string(status) + ")");
}

std::string CommandHandler::cmdNumberOfAmps(int64_t ampId, int16_t channel, const std::string& value) {
    return responseWithData("(amp_server_status_info (number_of_amps 1))");
}

std::string CommandHandler::cmdNumberOfActiveAmps(int64_t ampId, int16_t channel, const std::string& value) {
    int active = amplifier_->isPowered() ? 1 : 0;
    return responseWithData("(amp_server_status_info (number_of_active_amps " + std::to_string(active) + "))");
}

std::string CommandHandler::cmdListenToAmp(int64_t ampId, int16_t channel, const std::string& value) {
    return successResponse();
}

std::string CommandHandler::cmdStopListeningToAmp(int64_t ampId, int16_t channel, const std::string& value) {
    return successResponse();
}

std::string CommandHandler::cmdReceiveNotifications(int64_t ampId, int16_t channel, const std::string& value) {
    int clientId = ++nextClientId_;
    if (notificationHandler_) {
        notificationHandler_->sendClientConnected(clientId);
    }
    return responseWithData("(amp_server_status_info (client_connected (client_id " +
                           std::to_string(clientId) + ")))");
}

std::string CommandHandler::cmdStopReceivingNotifications(int64_t ampId, int16_t channel, const std::string& value) {
    return successResponse();
}

std::string CommandHandler::cmdExit(int64_t ampId, int16_t channel, const std::string& value) {
    exitRequested_ = true;
    return successResponse();
}

std::string CommandHandler::cmdDefaultAcquisitionState(int64_t ampId, int16_t channel, const std::string& value) {
    amplifier_->setDefaultAcquisitionState();
    return successResponse();
}

std::string CommandHandler::cmdDefaultSignalGeneration(int64_t ampId, int16_t channel, const std::string& value) {
    amplifier_->setDefaultSignalGenerationState();
    return successResponse();
}

// GTEN commands
std::string CommandHandler::cmdGTENSetTrain(int64_t ampId, int16_t channel, const std::string& value) {
    bool success = amplifier_->setGTENTrain(value);
    if (notificationHandler_) {
        notificationHandler_->sendGTENSetTrainResult(success);
    }
    return success ? successResponse() : errorResponse();
}

std::string CommandHandler::cmdGTENSetWaveforms(int64_t ampId, int16_t channel, const std::string& value) {
    bool success = amplifier_->setGTENWaveforms(value);
    if (notificationHandler_) {
        notificationHandler_->sendGTENSetWaveformResult(success);
    }
    return success ? successResponse() : errorResponse();
}

std::string CommandHandler::cmdGTENSetBlocks(int64_t ampId, int16_t channel, const std::string& value) {
    bool success = amplifier_->setGTENBlocks(value);
    return success ? successResponse() : errorResponse();
}

std::string CommandHandler::cmdGTENStartTrain(int64_t ampId, int16_t channel, const std::string& value) {
    bool success = amplifier_->startGTENTrain();
    if (notificationHandler_) {
        notificationHandler_->sendGTENStartTrainResult(success);
    }
    return success ? successResponse() : errorResponse();
}

std::string CommandHandler::cmdGTENAbortTrain(int64_t ampId, int16_t channel, const std::string& value) {
    bool success = amplifier_->abortGTENTrain();
    if (notificationHandler_) {
        notificationHandler_->sendGTENAbortTrainResult(success);
    }
    return success ? successResponse() : errorResponse();
}

std::string CommandHandler::cmdGTENGetStatus(int64_t ampId, int16_t channel, const std::string& value) {
    if (notificationHandler_) {
        notificationHandler_->sendGTENStatus();
    }
    return successResponse();
}

std::string CommandHandler::cmdGTENResetAlarm(int64_t ampId, int16_t channel, const std::string& value) {
    bool success = amplifier_->resetGTENAlarm();
    if (notificationHandler_) {
        notificationHandler_->sendGTENResetAlarmResult(success);
    }
    return success ? successResponse() : errorResponse();
}

} // namespace mock
