#ifndef EGIAMP_REMOTECONTROL_H
#define EGIAMP_REMOTECONTROL_H

#include "AmpServerConfig.h"
#include "EGIAmpClient.h"

#include <asio.hpp>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace egiamp {

/// What the remote control service drives. The CLI and GUI each implement it;
/// methods are called on the service's thread. Methods returning a string
/// return an empty string on success, otherwise the error message.
class RemoteControlTarget {
public:
    virtual ~RemoteControlTarget() = default;
    virtual EGIAmpClient& client() = 0;
    /// The configuration the next link would use.
    virtual AmpServerConfig config() = 0;
    virtual void setConfig(const AmpServerConfig& config) = 0;
    /// Connect and start streaming; blocks until done.
    virtual std::string link() = 0;
    virtual std::string unlink() = 0;
    virtual std::string setImpedance(bool on) = 0;
};

/// Executes one remote control command line and returns the reply line
/// ("OK[ ...]" or "ERROR <message>"). See README "Remote Control".
std::string handleRemoteCommand(RemoteControlTarget& target, const std::string& line);

/// Serves newline-delimited remote control commands on 127.0.0.1:<port>, one
/// client at a time, on its own thread. Throws std::system_error if the port
/// cannot be bound.
// ponytail: localhost only, no authentication; add a bind address (and auth)
// if control from another machine is ever needed.
class RemoteControlServer {
public:
    RemoteControlServer(RemoteControlTarget& target, uint16_t port);
    ~RemoteControlServer();

    RemoteControlServer(const RemoteControlServer&) = delete;
    RemoteControlServer& operator=(const RemoteControlServer&) = delete;

private:
    void serve();

    RemoteControlTarget& target_;
    asio::io_context io_;
    asio::ip::tcp::acceptor acceptor_;
    std::atomic<bool> stopping_{false};
    std::mutex clientMutex_;
    std::shared_ptr<asio::ip::tcp::socket> client_;
    std::thread thread_;
};

} // namespace egiamp

#endif // EGIAMP_REMOTECONTROL_H
