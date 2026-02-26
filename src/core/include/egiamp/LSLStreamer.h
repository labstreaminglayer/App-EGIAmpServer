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

    void createOutlet(const std::string& streamName, int eegChannelCount,
                      int physioChannelCount, int dinChannelCount, int sampleRate,
                      const std::string& hostname,
                      const AmplifierDetails& details,
                      bool nativeFormat = false);

    void createImpedanceOutlet(const std::string& streamName, int channelCount,
                               const std::string& hostname,
                               const AmplifierDetails& details);

    // Push a single sample (timestamp taken at call time).
    void pushSample(const std::vector<float>& sample);
    void pushSampleInt32(const std::vector<int32_t>& sample);

    // Push a chunk of samples with an explicit timestamp (from lsl::local_clock())
    // associated with the LAST sample.  LSL infers earlier samples' timestamps
    // from the declared sample rate.
    void pushChunk(const std::vector<std::vector<float>>& chunk, double timestamp);
    void pushChunkInt32(const std::vector<std::vector<int32_t>>& chunk, double timestamp);

    bool isNativeFormat() const { return nativeFormat_; }

    bool hasOutlet() const { return outlet_ != nullptr; }

    // Set timestamp offset in seconds (subtracted from current time when pushing)
    void setTimestampOffset(double offsetSeconds) { timestampOffset_ = offsetSeconds; }
    double getTimestampOffset() const { return timestampOffset_; }

    void closeOutlet();

private:
    std::unique_ptr<lsl::stream_outlet> outlet_;
    bool nativeFormat_ = false;
    double timestampOffset_ = 0.0;  // Filter delay compensation in seconds
};

} // namespace egiamp

#endif // EGIAMP_LSLSTREAMER_H
