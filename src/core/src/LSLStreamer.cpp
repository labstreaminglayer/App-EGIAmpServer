#include "egiamp/LSLStreamer.h"
#include "egiamp/EGIAmpClient.h"

namespace egiamp {

LSLStreamer::LSLStreamer() = default;

LSLStreamer::~LSLStreamer() {
    closeOutlet();
}

namespace {

// Get the montage filename for a given net code and channel count
std::string getMontageFilename(NetCode netCode, int channelCount) {
    // Determine if this net includes Cz as an extra channel
    // Standard nets: 32, 64, 128, 256 (Cz is reference, not recorded)
    // Extended nets: 33, 65, 129, 257 (Cz is recorded as last channel)
    bool hasCz = false;
    int baseCount = channelCount;

    switch (netCode) {
        case NetCode::HCGSN32_1_0:
        case NetCode::MCGSN32_1_0:
            baseCount = 32;
            hasCz = (channelCount == 33);
            break;
        case NetCode::GSN64_2_0:
        case NetCode::HCGSN64_1_0:
        case NetCode::MCGSN64_1_0:
            baseCount = 64;
            hasCz = (channelCount == 65);
            break;
        case NetCode::GSN128_2_0:
        case NetCode::HCGSN128_1_0:
        case NetCode::MCGSN128_1_0:
            baseCount = 128;
            hasCz = (channelCount == 129);
            break;
        case NetCode::GSN256_2_0:
        case NetCode::HCGSN256_1_0:
        case NetCode::MCGSN256_1_0:
            baseCount = 256;
            hasCz = (channelCount == 257);
            break;
        default:
            break;
    }

    int fileCount = hasCz ? baseCount + 1 : baseCount;
    return "GSN-HydroCel-" + std::to_string(fileCount) + ".sfp";
}

// Get cap/sensor name for metadata
std::string getCapName(NetCode netCode) {
    switch (netCode) {
        case NetCode::GSN64_2_0:
            return "Geodesic Sensor Net 64 2.0";
        case NetCode::GSN128_2_0:
            return "Geodesic Sensor Net 128 2.0";
        case NetCode::GSN256_2_0:
            return "Geodesic Sensor Net 256 2.0";
        case NetCode::HCGSN32_1_0:
            return "HydroCel Geodesic Sensor Net 32 1.0";
        case NetCode::HCGSN64_1_0:
            return "HydroCel Geodesic Sensor Net 64 1.0";
        case NetCode::HCGSN128_1_0:
            return "HydroCel Geodesic Sensor Net 128 1.0";
        case NetCode::HCGSN256_1_0:
            return "HydroCel Geodesic Sensor Net 256 1.0";
        case NetCode::MCGSN32_1_0:
            return "MicroCel Geodesic Sensor Net 32 1.0";
        case NetCode::MCGSN64_1_0:
            return "MicroCel Geodesic Sensor Net 64 1.0";
        case NetCode::MCGSN128_1_0:
            return "MicroCel Geodesic Sensor Net 128 1.0";
        case NetCode::MCGSN256_1_0:
            return "MicroCel Geodesic Sensor Net 256 1.0";
        default:
            return "Unknown";
    }
}

} // anonymous namespace

void LSLStreamer::createOutlet(const std::string& streamName, int eegChannelCount,
                               int physioChannelCount, int dinChannelCount, int sampleRate,
                               const std::string& hostname,
                               const AmplifierDetails& details,
                               bool nativeFormat) {
    // Close existing outlet if any
    closeOutlet();

    nativeFormat_ = nativeFormat;
    int totalChannelCount = eegChannelCount + physioChannelCount + dinChannelCount;

    // Create stream info with unique source ID
    // Include all parameters that make streams incompatible so clients
    // won't auto-reconnect when these change
    std::string formatSuffix = nativeFormat ? "_i32" : "_f32";
    std::string sourceId = "EGI_" + hostname +
                           "_ch" + std::to_string(totalChannelCount) +
                           "_sr" + std::to_string(sampleRate) +
                           formatSuffix;
    lsl::channel_format_t channelFormat = nativeFormat ? lsl::cf_int32 : lsl::cf_float32;
    lsl::stream_info info(streamName, "EEG", totalChannelCount,
                          static_cast<double>(sampleRate),
                          channelFormat, sourceId);

    // Get the description root
    lsl::xml_element desc = info.desc();

    // =========================================================================
    // Acquisition metadata
    // =========================================================================
    lsl::xml_element acq = desc.append_child("acquisition");
    acq.append_child_value("manufacturer", "Magstim EGI");
    acq.append_child_value("model", amplifierTypeName(details.amplifierType));
    if (!details.serialNumber.empty()) {
        acq.append_child_value("serial_number", details.serialNumber.c_str());
    }
    if (!details.firmwareVersion.empty()) {
        acq.append_child_value("firmware_version", details.firmwareVersion.c_str());
    }
    acq.append_child_value("precision", "24");

    // =========================================================================
    // Cap/sensor metadata
    // =========================================================================
    lsl::xml_element cap = desc.append_child("cap");
    cap.append_child_value("name", getCapName(details.netCode).c_str());
    cap.append_child_value("manufacturer", "Magstim EGI");
    cap.append_child_value("labeling_scheme", "E1, E2, ...");

    // Sensor material properties
    switch (details.netCode) {
        case NetCode::HCGSN32_1_0:
        case NetCode::HCGSN64_1_0:
        case NetCode::HCGSN128_1_0:
        case NetCode::HCGSN256_1_0:
            cap.append_child_value("coupling", "Saline");
            cap.append_child_value("material", "Ag-AgCl");
            cap.append_child_value("surface", "Sponge");
            break;
        case NetCode::MCGSN32_1_0:
        case NetCode::MCGSN64_1_0:
        case NetCode::MCGSN128_1_0:
        case NetCode::MCGSN256_1_0:
            cap.append_child_value("coupling", "Saline");
            cap.append_child_value("material", "Ag-AgCl");
            cap.append_child_value("surface", "Sponge");
            break;
        default:
            cap.append_child_value("coupling", "Saline");
            cap.append_child_value("material", "Ag-AgCl");
            break;
    }

    // =========================================================================
    // Reference metadata
    // =========================================================================
    lsl::xml_element ref = desc.append_child("reference");
    ref.append_child_value("label", "Cz");
    ref.append_child_value("subtracted", "Yes");

    // =========================================================================
    // Channel metadata
    // =========================================================================
    lsl::xml_element channels = desc.append_child("channels");

    // EEG channels
    for (int i = 0; i < eegChannelCount; i++) {
        lsl::xml_element ch = channels.append_child("channel");

        // Channel label: E1, E2, ... E256, or Cz for last channel if extended net
        std::string label;
        if (i == eegChannelCount - 1 && (eegChannelCount == 33 || eegChannelCount == 65 ||
                                         eegChannelCount == 129 || eegChannelCount == 257)) {
            label = "Cz";
        } else {
            label = "E" + std::to_string(i + 1);
        }
        ch.append_child_value("label", label.c_str());
        ch.append_child_value("type", "EEG");

        if (nativeFormat) {
            ch.append_child_value("unit", "counts");
            // Conversion factor: multiply by this to get microvolts
            if (details.scalingFactor != 0) {
                ch.append_child_value("conversion",
                                      std::to_string(details.scalingFactor).c_str());
            }
        } else {
            ch.append_child_value("unit", "microvolts");
        }
    }

    // Physio16 (PIB) channels
    for (int i = 0; i < physioChannelCount; i++) {
        lsl::xml_element ch = channels.append_child("channel");

        // Channel label: PIB1, PIB2, ... PIB32
        std::string label = "PIB" + std::to_string(i + 1);
        ch.append_child_value("label", label.c_str());
        ch.append_child_value("type", "AUX");

        if (nativeFormat) {
            ch.append_child_value("unit", "counts");
            // PIB channels 1-8 use negative scaling, 9-16 use positive scaling
            // Channel index within each PIB port: 0-7 negative, 8-15 positive
            int portChannel = i % 16;
            float conversion = (portChannel < 8) ? PHYSIO_SCALING_1_8 : PHYSIO_SCALING_9_16;
            ch.append_child_value("conversion",
                                  std::to_string(conversion).c_str());
        } else {
            ch.append_child_value("unit", "microvolts");
        }
    }

    // Digital input (DIN) channel - raw 16-bit value from amplifier's digital I/O
    if (dinChannelCount > 0) {
        lsl::xml_element ch = channels.append_child("channel");
        ch.append_child_value("label", "DIN");
        ch.append_child_value("type", "DIN");
        ch.append_child_value("unit", "uint16");
    }

    // =========================================================================
    // Filtering metadata (amplifier has built-in filters)
    // =========================================================================
    lsl::xml_element filtering = desc.append_child("filtering");

    // Anti-alias lowpass filter (varies by amp type and sample rate)
    lsl::xml_element lowpass = filtering.append_child("lowpass");
    lowpass.append_child_value("type", "Analog");
    // NA400/410 have 400Hz anti-alias filter at 1000 Hz sample rate
    // The cutoff scales with sample rate
    if (details.amplifierType == AmplifierType::NA400 ||
        details.amplifierType == AmplifierType::NA410) {
        double cutoff = sampleRate * 0.4;  // Approximately 40% of sample rate
        lowpass.append_child_value("cutoff", std::to_string(cutoff).c_str());
    }

    // DC blocking highpass
    lsl::xml_element highpass = filtering.append_child("highpass");
    highpass.append_child_value("type", "Analog");
    highpass.append_child_value("cutoff", "0.1");  // DC-coupled with ~0.1 Hz highpass

    // Create outlet (transmit at least every SAMPLES_PER_CHUNK samples)
    outlet_ = std::make_unique<lsl::stream_outlet>(info, SAMPLES_PER_CHUNK);
}

void LSLStreamer::createImpedanceOutlet(const std::string& streamName, int channelCount,
                                         const std::string& hostname,
                                         const AmplifierDetails& details) {
    // Close existing outlet if any
    closeOutlet();

    // Create stream info with unique source ID for impedance
    // Use irregular rate (0) since impedance samples only come when current is injecting
    std::string sourceId = "EGI_" + hostname +
                           "_ch" + std::to_string(channelCount) +
                           "_impedance";
    lsl::stream_info info(streamName, "Impedance", channelCount,
                          0.0,  // Irregular rate
                          lsl::cf_float32, sourceId);

    // Get the description root
    lsl::xml_element desc = info.desc();

    // Acquisition metadata
    lsl::xml_element acq = desc.append_child("acquisition");
    acq.append_child_value("manufacturer", "Magstim EGI");
    acq.append_child_value("model", amplifierTypeName(details.amplifierType));
    if (!details.serialNumber.empty()) {
        acq.append_child_value("serial_number", details.serialNumber.c_str());
    }

    // Channel metadata
    lsl::xml_element channels = desc.append_child("channels");

    for (int i = 0; i < channelCount; i++) {
        lsl::xml_element ch = channels.append_child("channel");

        // Channel label: E1, E2, ... E256, or Cz for last channel if extended net
        std::string label;
        if (i == channelCount - 1 && (channelCount == 33 || channelCount == 65 ||
                                       channelCount == 129 || channelCount == 257)) {
            label = "Cz";
        } else {
            label = "E" + std::to_string(i + 1);
        }
        ch.append_child_value("label", label.c_str());
        ch.append_child_value("type", "Impedance");
        ch.append_child_value("unit", "volts");
    }

    // Add description note
    desc.append_child("description").append_child_value("note",
        "Compliance voltage values. Divide by drive current to obtain impedance in ohms.");

    // Create outlet
    outlet_ = std::make_unique<lsl::stream_outlet>(info, SAMPLES_PER_CHUNK);
}

void LSLStreamer::pushSample(const std::vector<float>& sample) {
    if (outlet_) {
        if (timestampOffset_ != 0.0) {
            // Apply filter delay compensation
            double adjustedTime = lsl::local_clock() - timestampOffset_;
            outlet_->push_sample(sample, adjustedTime);
        } else {
            outlet_->push_sample(sample);
        }
    }
}

void LSLStreamer::pushSampleInt32(const std::vector<int32_t>& sample) {
    if (outlet_) {
        if (timestampOffset_ != 0.0) {
            // Apply filter delay compensation
            double adjustedTime = lsl::local_clock() - timestampOffset_;
            outlet_->push_sample(sample, adjustedTime);
        } else {
            outlet_->push_sample(sample);
        }
    }
}

void LSLStreamer::closeOutlet() {
    outlet_.reset();
}

} // namespace egiamp
