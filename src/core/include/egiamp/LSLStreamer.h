#ifndef EGIAMP_LSLSTREAMER_H
#define EGIAMP_LSLSTREAMER_H

#include "AmpServerProtocol.h"

#include <memory>
#include <string>
#include <vector>

#include <lsl_cpp.h>

namespace egiamp {

// Forward declaration
struct AmplifierDetails;

class LSLStreamer {
public:
    LSLStreamer();
    ~LSLStreamer();

    LSLStreamer(const LSLStreamer&) = delete;
    LSLStreamer& operator=(const LSLStreamer&) = delete;

    void createOutlet(const std::string& streamName, int channelCount,
                      int sampleRate, const std::string& hostname,
                      const AmplifierDetails& details);

    void createImpedanceOutlet(const std::string& streamName, int channelCount,
                               const std::string& hostname,
                               const AmplifierDetails& details);

    void pushSample(const std::vector<float>& sample);

    bool hasOutlet() const { return outlet_ != nullptr; }

    void closeOutlet();

private:
    std::unique_ptr<lsl::stream_outlet> outlet_;
};

} // namespace egiamp

#endif // EGIAMP_LSLSTREAMER_H
