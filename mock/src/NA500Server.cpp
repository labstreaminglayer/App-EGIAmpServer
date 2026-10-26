#include "NA500Server.h"
#include "ByteSwap.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <iterator>
#include <set>

namespace mock {

namespace {

constexpr int SAMPLES_PER_BLOCK = 8;
constexpr std::size_t SEND_QUEUE_BLOCKS = 8;
// Above this the device drops frames silently: at 16 kHz timestamps still step
// 62 us and the counter steps 1, but only ~8000 packets/s arrive.
constexpr int MAX_DELIVERED_RATE = 8000;
// The amp's clock is not synced until a client sends cmd_SetAmpTime; the unit
// we recorded was about a month behind.
constexpr int64_t STALE_CLOCK_US = -30LL * 24 * 3600 * 1000000;

const std::string OK = "(sendCommand_return (status complete))";

const std::set<std::string> RATES = {"250", "500", "1000", "2000", "4000", "8000", "16000"};

// Commands the device recognizes (scripts/ampserver_conformance.py checks them,
// except cmd_SetCOMDriveSignal, cmd_SetCOMAndRefForMeasurement and
// cmd_SetScreenOrientation, which it skips to leave the unit alone).
// Anything else re-runs the previous command.
const std::set<std::string> KNOWN = {
    "cmd_None", "cmd_Start", "cmd_Stop", "cmd_TurnAll10KOhms", "cmd_TurnChannel10KOhms",
    "cmd_SetCOM10KOhms", "cmd_TurnAllDriveSignals", "cmd_SetCOMDriveSignal",
    "cmd_TurnChannelDriveSignals", "cmd_SetSubjectGround", "cmd_SetCurrentSource",
    "cmd_SetCalibrationSignalFreq", "cmd_SetBufferedReference", "cmd_SetOscillatorGate",
    "cmd_SetReference10KOhms", "cmd_SetReferenceDriveSignal", "cmd_SetPower", "cmd_Reset",
    "cmd_SetWaveShape", "cmd_SetDrivenCommon", "cmd_SetCalibrationSignalAmplitude",
    "cmd_SetAnalogOutput", "cmd_SetDigitalOutputData", "cmd_SetDigitalInOutDirection",
    "cmd_GetStartTime", "cmd_GetCurrentTime", "cmd_GetCurrentDrift", "cmd_SetNativeRate",
    "cmd_SetDecimatedRate", "cmd_SetMRIPulseInfo", "cmd_SetPIBChannelGain",
    "cmd_TurnChannelZeroOhms", "cmd_TurnAllZeroOhms", "cmd_SetPhoticStimSequence",
    "cmd_GetPhysioConnectionStatus", "cmd_GetAmpDetails", "cmd_GetAmpStatus",
    "cmd_DefaultAcquisitionState", "cmd_DefaultSignalGeneration", "cmd_NumberOfAmps",
    "cmd_NumberOfActiveAmps", "cmd_ListenToAmp", "cmd_StopListeningToAmp",
    "cmd_ReceiveNotifications", "cmd_StopReceivingNotifications", "cmd_Exit",
    "cmd_GetBatteryData", "cmd_GetBoardStatus", "cmd_SetSpo2Enable", "cmd_SetDinEnable",
    "cmd_SetTestMode", "cmd_SetCOMAndRefForMeasurement", "cmd_SetOpticalClockMode",
    "cmd_SetRecordingState", "cmd_SetAmpTime", "cmd_SetBncInMode", "cmd_SetBncOutMode",
    "cmd_SetBncOutPulseWidth", "cmd_SetBlankingPulseWidth", "cmd_SetTriggerBncManualOut",
    "cmd_SetQuietMode", "cmd_SetScreenOrientation"};

int64_t hostMicros() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// Poll a non-blocking acceptor until a client arrives or `keepGoing` turns false.
template <typename Pred>
std::shared_ptr<asio::ip::tcp::socket> acceptOne(asio::io_context& io, asio::ip::tcp::acceptor& acceptor,
                                                 Pred keepGoing) {
    while (keepGoing()) {
        auto socket = std::make_shared<asio::ip::tcp::socket>(io);
        asio::error_code ec;
        acceptor.accept(*socket, ec);
        if (!ec) {
            socket->set_option(asio::ip::tcp::no_delay(true), ec);
            return socket;
        }
        // Poll quickly: the device accepts within microseconds, and clients
        // send commands right after connecting all three ports.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return nullptr;
}

asio::ip::tcp::acceptor listen(asio::io_context& io, uint16_t port) {
    asio::ip::tcp::acceptor acceptor(io);
    const asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port);
    acceptor.open(endpoint.protocol());
    acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
    acceptor.bind(endpoint);
    acceptor.listen(5);
    acceptor.non_blocking(true);
    return acceptor;
}

}  // namespace

NA500Server::NA500Server(std::shared_ptr<MockAmplifier> amplifier,
                         uint16_t cmdPort, uint16_t notifyPort, uint16_t dataPort)
    : amp_(std::move(amplifier)), cmdPort_(cmdPort), notifyPort_(notifyPort), dataPort_(dataPort),
      clockOffsetUs_(STALE_CLOCK_US) {
    streamThread_ = std::thread(&NA500Server::streamLoop, this);
    sendThread_ = std::thread(&NA500Server::sendLoop, this);
}

NA500Server::~NA500Server() {
    stopping_ = true;
    queueCv_.notify_all();
    if (streamThread_.joinable()) streamThread_.join();
    if (sendThread_.joinable()) sendThread_.join();
}

int64_t NA500Server::ampClockMicros() const {
    return hostMicros() + clockOffsetUs_;
}

void NA500Server::run(const std::atomic<bool>& running) {
    std::cout << "[NA500] Serving one session at a time on ports " << cmdPort_ << "/"
              << notifyPort_ << "/" << dataPort_ << std::endl;
    while (running) {
        session(running);
    }
    stopping_ = true;
}

void NA500Server::session(const std::atomic<bool>& running) {
    auto cmdAcceptor = listen(io_, cmdPort_);
    auto notifyAcceptor = listen(io_, notifyPort_);
    auto dataAcceptor = listen(io_, dataPort_);

    // All three ports accept concurrently, each exactly one client per session.
    std::atomic<bool> open{true};
    auto acceptInto = [&](asio::ip::tcp::acceptor& acceptor, std::shared_ptr<Socket>& slot) {
        return std::thread([this, &open, acc = &acceptor, out = &slot] {
            auto socket = acceptOne(io_, *acc, [&] { return open.load(); });
            std::lock_guard<std::mutex> lock(socketMutex_);
            *out = socket;
        });
    };
    std::thread notifyAccept = acceptInto(notifyAcceptor, notifySocket_);
    std::thread dataAccept = acceptInto(dataAcceptor, dataSocket_);

    auto cmd = acceptOne(io_, cmdAcceptor, [&] { return running.load(); });
    if (cmd) std::cout << "[NA500] Session started" << std::endl;

    char buffer[4096];
    if (cmd) cmd->non_blocking(true);
    while (cmd && running) {
        asio::error_code ec;
        const std::size_t n = cmd->read_some(asio::buffer(buffer), ec);
        if (ec == asio::error::would_block) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        if (ec || n == 0) break;

        // One read is one command. Fields that fail to parse keep the previous
        // command's values; an unknown name re-runs the previous command.
        char name[64] = {0};
        long long values[3] = {lastValues_[0], lastValues_[1], lastValues_[2]};
        const std::string segment(buffer, n);
        const int parsed = std::sscanf(segment.c_str(), "(sendCommand %63s %lld %lld %lld)",
                                       name, &values[0], &values[1], &values[2]);
        // The parsed values are kept even when the name is not recognized.
        if (parsed >= 1 && KNOWN.count(name)) lastName_ = name;
        std::copy(values, values + 3, lastValues_);
        for (const auto& reply : execute(lastName_, lastValues_[1], lastValues_[2])) {
            asio::write(*cmd, asio::buffer(reply + "\n"), ec);
        }
        for (const auto& note : pendingNotifications_) notify(note);  // replies go first
        pendingNotifications_.clear();
    }

    // Session teardown: like the device, stop, power off and drop every client.
    open = false;
    notifyAccept.join();
    dataAccept.join();
    // The DIN enable survives into the next session, as on the device.
    streaming_ = false;
    bufferedReference_ = false;
    amp_->setPower(false);
    {
        std::lock_guard<std::mutex> lock(socketMutex_);
        notifySocket_.reset();
        asio::error_code ec;
        if (dataSocket_) dataSocket_->shutdown(Socket::shutdown_both, ec);  // unblocks sendLoop
        dataSocket_.reset();
    }
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        queue_.clear();
    }
    std::cout << "[NA500] Session ended" << std::endl;
}

void NA500Server::notify(const std::string& line) {
    std::lock_guard<std::mutex> lock(socketMutex_);
    if (!notifySocket_) return;
    asio::error_code ec;
    asio::write(*notifySocket_, asio::buffer(line + "\n"), ec);
}

std::vector<std::string> NA500Server::execute(const std::string& name, long long channel,
                                              long long value) {
    const bool on = value == 1;
    const std::string v = std::to_string(value);

    if (name == "cmd_SetPower") {
        if (value != 0 && value != 1) return {};
        amp_->setPower(on);
        if (on) poweredOnAt_ = std::chrono::steady_clock::now();
        if (!on) streaming_ = false;
        pendingNotifications_.push_back(on ? "(notification ntn_AmpPowerOn 9 0 -1)"
                                           : "(notification ntn_AmpPowerOff 8 0 -1)");
        return {OK};
    }
    if (name == "cmd_Start") {
        streamStartUs_ = ampClockMicros();
        streaming_ = true;
        return {OK};
    }
    if (name == "cmd_Stop") {
        streaming_ = false;
        bufferedReference_ = false;  // cmd_Stop switches the front end off
        std::this_thread::sleep_for(std::chrono::seconds(1));
        pendingNotifications_.push_back("(notification ntn_AmpStopped 11 0 -1)");
        return {OK};
    }
    if (name == "cmd_SetDecimatedRate" || name == "cmd_SetNativeRate") {
        if (!RATES.count(v)) return {};
        if (name == "cmd_SetDecimatedRate") amp_->setDecimatedRate(static_cast<int>(value));
        else amp_->setNativeRate(static_cast<int>(value));
        return {OK};
    }
    if (name == "cmd_SetTestMode") {
        return (value == 0 || value == 1 || (value >= 3 && value <= 5)) ? std::vector<std::string>{OK}
                                                                        : std::vector<std::string>{};
    }
    if (name == "cmd_ListenToAmp" || name == "cmd_StopListeningToAmp") return {};
    if (name == "cmd_TurnChannelZeroOhms" || name == "cmd_TurnAllZeroOhms" ||
        name == "cmd_SetMRIPulseInfo" || name == "cmd_SetPhoticStimSequence") {
        return {OK, OK};
    }

    if (name == "cmd_NumberOfAmps") {
        return {"(sendCommand_return (status complete) (number_of_amps 1))"};
    }
    if (name == "cmd_GetAmpDetails") {
        // The device omits the final closing parenthesis.
        const auto& s = amp_->state();
        return {"(sendCommand_return (status complete) (amp_details (serial_number " + s.serialNumber +
                ") (amp_type NA500) (legacy_board false) (packet_format 2) (system_version 0.4.9)"
                " (fpga_version 0.6.1.f0) (pcb_version 4.1.0.0) (number_of_channels 256)"
                " (feature_flags 0))"};
    }
    if (name == "cmd_GetBatteryData") {
        // Unbalanced on the device too: one closing parenthesis short.
        return {"(sendCommand_return (status complete) (battery_details (time_remaining 600)"
                " (master 100) (slave1 100) (slave2 100) (slave3 100) (slave4 100))"};
    }
    if (name == "cmd_GetBoardStatus") {
        // One closing parenthesis too many, as on the device.
        return {"(sendCommand_return (status complete) (status_details (ambient_temp 3200)"
                " (lpd_temp 40000) (fpd_temp 40000) (pl_temp 40000))))"};
    }
    if (name == "cmd_GetCurrentTime") {
        return {"(sendCommand_return (status complete) (amp_status_info (current_time " +
                std::to_string(packetCounter_.load()) + ")))"};
    }
    if (name == "cmd_SetRecordingState") {
        return {"(sendCommand_return (status complete) (amp_status_info (start_time " +
                std::to_string(packetCounter_.load()) + ")))"};
    }
    if (name == "cmd_GetStartTime") {
        pendingNotifications_.push_back("(notification ntn_AmpStarted 10 0 -1)");
        return {"(sendCommand_return (status complete) (amp_status_info (start_time " +
                std::to_string(streamStartUs_.load()) + ")))"};
    }
    if (name == "cmd_SetAmpTime") {
        clockOffsetUs_ = value - hostMicros();
        return {OK};
    }
    if (name == "cmd_SetDinEnable") {
        dinEnabled_ = true;  // the device ignores the value
        return {OK};
    }

    // Front-end state the mock's synthetic signals depend on (impedance mode).
    const int ch = static_cast<int>(channel);
    if (name == "cmd_TurnAll10KOhms") amp_->setAll10KOhms(on);
    else if (name == "cmd_TurnChannel10KOhms") amp_->setChannel10KOhms(ch, on);
    else if (name == "cmd_TurnAllDriveSignals") amp_->setAllDriveSignals(on);
    else if (name == "cmd_TurnChannelDriveSignals") amp_->setChannelDriveSignals(ch, on);
    else if (name == "cmd_SetCOM10KOhms") amp_->setCom10KOhms(on);
    else if (name == "cmd_SetReference10KOhms") amp_->setReference10KOhms(on);
    else if (name == "cmd_SetReferenceDriveSignal") amp_->setReferenceDriveSignal(on);
    else if (name == "cmd_SetCOMDriveSignal") amp_->setComDriveSignal(on);
    else if (name == "cmd_SetSubjectGround") amp_->setSubjectGround(on);
    else if (name == "cmd_SetDrivenCommon") amp_->setDrivenCommon(on);
    else if (name == "cmd_SetBufferedReference") {
        // The front end is still powering up for ~20 ms after cmd_SetPower 1
        // and silently drops switch commands (the reply is still OK).
        if (std::chrono::steady_clock::now() - poweredOnAt_ >= std::chrono::milliseconds(20)) {
            bufferedReference_ = on;
        }
    }
    else if (name == "cmd_SetOscillatorGate") amp_->setOscillatorGate(on);
    else if (name == "cmd_SetWaveShape") amp_->setWaveShape(static_cast<WaveShape>(value));
    else if (name == "cmd_SetCalibrationSignalFreq") amp_->setCalibrationSignalFreq(static_cast<int>(value));
    else if (name == "cmd_SetCalibrationSignalAmplitude") amp_->setCalibrationSignalAmplitude(static_cast<int>(value));
    else if (name == "cmd_SetPIBChannelGain") amp_->setPIBChannelGain(ch, static_cast<int>(value));
    return {OK};
}

void NA500Server::streamLoop() {
    // Blocks are filled one sample at a time and only sent when full. A block left
    // partly filled by cmd_Stop is kept (even across sessions) and completed by the
    // next stream, so each new stream opens with a few stale packets, as on the device.
    std::vector<uint8_t> payload(SAMPLES_PER_BLOCK * PACKET_FORMAT2_SIZE);
    int fill = 0;
    auto next = std::chrono::steady_clock::now();
    uint64_t framesSinceStart = 0;
    bool wasStreaming = false;

    while (!stopping_) {
        if (!(streaming_ && amp_->isPowered())) {
            wasStreaming = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        if (!wasStreaming) {
            wasStreaming = true;
            framesSinceStart = 0;
            packetCounter_ = 0;  // every cmd_Start restarts the counter
            next = std::chrono::steady_clock::now();
        }

        const int rate = amp_->state().activeRate();
        const int64_t stepUs = 1000000 / rate;  // integer step: 62 us at 16 kHz, as on the device
        const int delivered = rate < MAX_DELIVERED_RATE ? rate : MAX_DELIVERED_RATE;
        const auto period = std::chrono::nanoseconds(1000000000LL / delivered);

        while (streaming_ && std::chrono::steady_clock::now() >= next) {
            next += period;
            PacketFormat2_SamplePacket packet;
            amp_->generatePacketFormat2(packet);
            packet.tr = 0;
            packet.packetCounter = packetCounter_++;
            packet.timeStamp = static_cast<uint64_t>(streamStartUs_ + framesSinceStart++ * stepUs);
            packet.netCode = static_cast<uint8_t>(amp_->state().netCode);
            if (!dinEnabled_) packet.digitalInputs = 0;
            if (!bufferedReference_) std::fill(std::begin(packet.eegData), std::end(packet.eegData), -8388608);
            packet.reserved[0] = 3;  // idle level of the event inputs on the recorded unit
            std::memcpy(payload.data() + fill * PACKET_FORMAT2_SIZE, &packet, PACKET_FORMAT2_SIZE);
            if (++fill < SAMPLES_PER_BLOCK) continue;
            fill = 0;

            const AmpDataPacketHeader header{0, bswap64(payload.size())};
            std::vector<uint8_t> block(sizeof(header) + payload.size());
            std::memcpy(block.data(), &header, sizeof(header));
            std::memcpy(block.data() + sizeof(header), payload.data(), payload.size());
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (queue_.size() < SEND_QUEUE_BLOCKS) queue_.push_back(std::move(block));
            queueCv_.notify_one();
        }
        std::this_thread::sleep_until(next);
    }
}

void NA500Server::sendLoop() {
    while (!stopping_) {
        std::vector<uint8_t> block;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait_for(lock, std::chrono::milliseconds(50),
                              [this] { return !queue_.empty() || stopping_; });
            if (queue_.empty()) continue;
            block = std::move(queue_.front());
            queue_.pop_front();
        }
        std::shared_ptr<Socket> socket;
        {
            std::lock_guard<std::mutex> lock(socketMutex_);
            socket = dataSocket_;
        }
        if (!socket) continue;  // no data client: the block is lost
        asio::error_code ec;
        asio::write(*socket, asio::buffer(block), ec);
        if (ec) {
            // The device never serves this port again this session.
            std::lock_guard<std::mutex> lock(socketMutex_);
            if (dataSocket_ == socket) dataSocket_.reset();
        }
    }
}

}  // namespace mock
