#include "egiamp/LSLStreamer.h"

namespace egiamp {

LSLStreamer::LSLStreamer() = default;

LSLStreamer::~LSLStreamer() {
    closeOutlet();
}

void LSLStreamer::createOutlet(const std::string& streamName, int channelCount,
                               int sampleRate, const std::string& hostname) {
    // Close existing outlet if any
    closeOutlet();

    // Create stream info with unique source ID
    std::string sourceId = streamName + "at_" + hostname;
    lsl::stream_info info(streamName, "EEG", channelCount,
                          static_cast<double>(sampleRate),
                          lsl::cf_float32, sourceId);

    // Append metadata
    info.desc()
        .append_child("acquisition")
        .append_child_value("manufacturer", "Philips Neuro")
        .append_child_value("model", "NetAmp")
        .append_child_value("application", "AmpServer")
        .append_child_value("precision", "24");

    // Create outlet (transmit at least every SAMPLES_PER_CHUNK samples)
    outlet_ = std::make_unique<lsl::stream_outlet>(info, SAMPLES_PER_CHUNK);
}

void LSLStreamer::pushSample(const std::vector<float>& sample) {
    if (outlet_) {
        outlet_->push_sample(sample);
    }
}

void LSLStreamer::closeOutlet() {
    outlet_.reset();
}

} // namespace egiamp
