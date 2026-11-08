#include "egiamp/RemoteControl.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace egiamp {

namespace {

const char* HELP =
    "OK commands: status | set <address|command_port|notification_port|data_port|sample_rate|"
    "fast_recovery> <value> | load <file> | save <file> | link | unlink | impedance <on|off> | "
    "trigger [width_us] | shutdown_ampserver | help";

std::string result(const std::string& error) {
    return error.empty() ? "OK" : "ERROR " + error;
}

int parseInt(const std::string& s, int lo, int hi) {
    size_t used = 0;
    const int v = std::stoi(s, &used);
    if (used != s.size() || v < lo || v > hi) throw std::invalid_argument(s);
    return v;
}

} // namespace

std::string handleRemoteCommand(RemoteControlTarget& target, const std::string& line) {
    std::istringstream in(line);
    std::string cmd;
    in >> cmd;
    std::string rest;
    std::getline(in >> std::ws, rest);
    while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.back()))) rest.pop_back();
    EGIAmpClient& client = target.client();
    const bool linked = client.isStreaming();

    try {
        if (cmd == "help") return HELP;

        if (cmd == "status") {
            const AmpServerConfig c = target.config();
            std::ostringstream out;
            out << "OK linked=" << linked << " impedance=" << client.isImpedanceModeActive()
                << " address=" << c.serverAddress << " command_port=" << c.commandPort
                << " notification_port=" << c.notificationPort << " data_port=" << c.dataPort
                << " sample_rate=" << c.sampleRate << " fast_recovery=" << c.fastRecovery;
            if (linked) {
                const auto& d = client.amplifierDetails();
                std::string model = amplifierTypeName(d.amplifierType);
                std::replace(model.begin(), model.end(), ' ', '_');
                out << " amplifier=" << model << " serial=" << d.serialNumber
                    << " net=" << netCodeName(d.netCode);
            }
            return out.str();
        }

        if (cmd == "set" || cmd == "load") {
            if (linked) return "ERROR unlink first";
            AmpServerConfig c = target.config();
            if (cmd == "load") {
                if (rest.empty()) return "ERROR usage: load <file>";
                c = AmpServerConfig::loadFromFile(rest);
            } else {
                std::istringstream args(rest);
                std::string key, value;
                if (!(args >> key >> value)) return "ERROR usage: set <key> <value>";
                if (key == "address") c.serverAddress = value;
                else if (key == "command_port") c.commandPort = static_cast<uint16_t>(parseInt(value, 1, 65535));
                else if (key == "notification_port") c.notificationPort = static_cast<uint16_t>(parseInt(value, 1, 65535));
                else if (key == "data_port") c.dataPort = static_cast<uint16_t>(parseInt(value, 1, 65535));
                else if (key == "sample_rate") {
                    c.sampleRate = parseInt(value, 1, 1000000);
                    c.forceSampleRate = true;  // an explicit request, as with the CLI's --sample-rate
                } else if (key == "fast_recovery") {
                    c.fastRecovery = parseInt(value, 0, 1) == 1;
                    c.forceSampleRate = true;
                } else return "ERROR unknown setting: " + key;
            }
            target.setConfig(c);
            return "OK";
        }

        if (cmd == "save") {
            if (rest.empty()) return "ERROR usage: save <file>";
            target.config().saveToFile(rest);
            return "OK";
        }

        if (cmd == "link") return linked ? "ERROR already linked" : result(target.link());
        if (cmd == "unlink") return linked ? result(target.unlink()) : "ERROR not linked";

        if (cmd == "impedance") {
            if (rest != "on" && rest != "off") return "ERROR usage: impedance <on|off>";
            if (!linked) return "ERROR not linked";
            return result(target.setImpedance(rest == "on"));
        }

        if (cmd == "trigger") {
            const int width = rest.empty() ? EGIAmpClient::BNC_OUT_DEFAULT_PULSE_US
                                           : parseInt(rest, EGIAmpClient::BNC_OUT_MIN_PULSE_US,
                                                      EGIAmpClient::BNC_OUT_MAX_PULSE_US);
            if (!linked) return "ERROR not linked";
            if (client.amplifierDetails().amplifierType != AmplifierType::NA500)
                return "ERROR the BNC output trigger needs a Net Amps 500";
            return client.triggerBncOut(width) ? "OK" : "ERROR trigger command failed";
        }

        if (cmd == "shutdown_ampserver") {
            if (linked) return "ERROR unlink first";
            client.setConfig(target.config());
            if (!client.connect()) return "ERROR could not connect to AmpServer";
            const bool ok = client.shutdownAmpServer();
            client.disconnect();
            return ok ? "OK" : "ERROR shutdown command failed";
        }
    } catch (const std::invalid_argument&) {
        return "ERROR invalid value: " + rest;
    } catch (const std::out_of_range&) {
        return "ERROR invalid value: " + rest;
    } catch (const std::exception& e) {
        return std::string("ERROR ") + e.what();
    }
    return "ERROR unknown command: " + cmd + " (try help)";
}

RemoteControlServer::RemoteControlServer(RemoteControlTarget& target, uint16_t port)
    : target_(target)
    , acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), port))
    , thread_([this] { serve(); }) {}

RemoteControlServer::~RemoteControlServer() {
    stopping_ = true;
    asio::error_code ec;
    {
        // Closing the acceptor does not wake a blocked accept() on macOS; a connection does.
        asio::ip::tcp::socket wake(io_);
        wake.connect(acceptor_.local_endpoint(ec), ec);
    }
    {
        std::lock_guard<std::mutex> lock(clientMutex_);
        if (client_) client_->shutdown(asio::ip::tcp::socket::shutdown_both, ec);  // unblocks read
    }
    thread_.join();
    acceptor_.close(ec);
}

void RemoteControlServer::serve() {
    while (!stopping_) {
        auto socket = std::make_shared<asio::ip::tcp::socket>(io_);
        asio::error_code ec;
        acceptor_.accept(*socket, ec);
        if (ec) {
            if (stopping_) return;
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(clientMutex_);
            if (stopping_) return;
            client_ = socket;
        }
        asio::streambuf buffer;
        while (!stopping_) {
            asio::read_until(*socket, buffer, '\n', ec);
            if (ec) break;
            std::istream stream(&buffer);
            std::string line;
            std::getline(stream, line);
            if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
            const std::string reply = handleRemoteCommand(target_, line) + "\n";
            asio::write(*socket, asio::buffer(reply), ec);
            if (ec) break;
        }
        std::lock_guard<std::mutex> lock(clientMutex_);
        client_.reset();
    }
}

} // namespace egiamp
