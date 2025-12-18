#ifndef MOCK_COMMANDHANDLER_H
#define MOCK_COMMANDHANDLER_H

#include "MockAmplifier.h"
#include <asio.hpp>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mock {

class NotificationHandler;

using NotificationCallback = std::function<void(const std::string&)>;

class CommandHandler : public std::enable_shared_from_this<CommandHandler> {
public:
    CommandHandler(asio::io_context& io_context, uint16_t port,
                   std::shared_ptr<MockAmplifier> amplifier,
                   std::shared_ptr<NotificationHandler> notificationHandler);
    ~CommandHandler();

    void start();
    void stop();

private:
    void acceptConnections();
    void handleConnection(std::shared_ptr<asio::ip::tcp::socket> socket);
    void readCommand(std::shared_ptr<asio::ip::tcp::socket> socket);

    std::string processCommand(const std::string& command);
    std::string parseAndExecuteCommand(const std::string& cmdLine);

    // Command implementations
    std::string cmdNone(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdStart(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdStop(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdSetPower(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdReset(int64_t ampId, int16_t channel, const std::string& value);

    std::string cmdTurnAll10KOhms(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdTurnChannel10KOhms(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdSetCOM10KOhms(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdSetReference10KOhms(int64_t ampId, int16_t channel, const std::string& value);

    std::string cmdTurnAllDriveSignals(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdTurnChannelDriveSignals(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdSetCOMDriveSignal(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdSetReferenceDriveSignal(int64_t ampId, int16_t channel, const std::string& value);

    std::string cmdSetSubjectGround(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdSetCurrentSource(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdSetCalibrationSignalFreq(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdSetBufferedReference(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdSetOscillatorGate(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdSetWaveShape(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdSetDrivenCommon(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdSetCalibrationSignalAmplitude(int64_t ampId, int16_t channel, const std::string& value);

    std::string cmdSetDigitalOutputData(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdSetDigitalInOutDirection(int64_t ampId, int16_t channel, const std::string& value);

    std::string cmdSetNativeRate(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdSetDecimatedRate(int64_t ampId, int16_t channel, const std::string& value);

    std::string cmdGetStartTime(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdGetAmpDetails(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdGetAmpStatus(int64_t ampId, int16_t channel, const std::string& value);

    std::string cmdTurnChannelZeroOhms(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdTurnAllZeroOhms(int64_t ampId, int16_t channel, const std::string& value);

    std::string cmdSetPIBChannelGain(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdGetPhysioConnectionStatus(int64_t ampId, int16_t channel, const std::string& value);

    std::string cmdNumberOfAmps(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdNumberOfActiveAmps(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdListenToAmp(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdStopListeningToAmp(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdReceiveNotifications(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdStopReceivingNotifications(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdExit(int64_t ampId, int16_t channel, const std::string& value);

    std::string cmdDefaultAcquisitionState(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdDefaultSignalGeneration(int64_t ampId, int16_t channel, const std::string& value);

    // GTEN commands
    std::string cmdGTENSetTrain(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdGTENSetWaveforms(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdGTENSetBlocks(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdGTENStartTrain(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdGTENAbortTrain(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdGTENGetStatus(int64_t ampId, int16_t channel, const std::string& value);
    std::string cmdGTENResetAlarm(int64_t ampId, int16_t channel, const std::string& value);

    // Helper
    std::string successResponse();
    std::string errorResponse();
    std::string responseWithData(const std::string& data);

    void sendNotification(const std::string& notification);

    asio::io_context& io_context_;
    asio::ip::tcp::acceptor acceptor_;
    std::shared_ptr<MockAmplifier> amplifier_;
    std::shared_ptr<NotificationHandler> notificationHandler_;
    std::atomic<bool> running_{false};
    std::atomic<bool> exitRequested_{false};
    int nextClientId_ = 0;

    using CommandFunc = std::function<std::string(CommandHandler*, int64_t, int16_t, const std::string&)>;
    std::unordered_map<std::string, CommandFunc> commandMap_;

    void initCommandMap();
};

} // namespace mock

#endif // MOCK_COMMANDHANDLER_H
