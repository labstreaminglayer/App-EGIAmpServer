#include "egiamp/AmpServerConfig.h"

#include <pugixml.hpp>

namespace egiamp {

AmpServerConfig AmpServerConfig::loadFromFile(const std::string& filename) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(filename.c_str());

    if (!result) {
        throw ConfigError(std::string("Cannot read config file: ") + result.description());
    }

    AmpServerConfig config;

    // Navigate to ampserver settings
    auto ampserver = doc.child("ampserver");
    if (ampserver) {
        if (auto node = ampserver.child("address")) {
            config.serverAddress = node.text().as_string(config.serverAddress.c_str());
        }
        if (auto node = ampserver.child("commandport")) {
            config.commandPort = static_cast<uint16_t>(node.text().as_int(config.commandPort));
        }
        if (auto node = ampserver.child("notificationport")) {
            config.notificationPort = static_cast<uint16_t>(node.text().as_int(config.notificationPort));
        }
        if (auto node = ampserver.child("dataport")) {
            config.dataPort = static_cast<uint16_t>(node.text().as_int(config.dataPort));
        }
    }

    // Navigate to settings
    auto settings = doc.child("settings");
    if (settings) {
        if (auto node = settings.child("amplifierid")) {
            config.amplifierId = node.text().as_int(config.amplifierId);
        }
        if (auto node = settings.child("samplingrate")) {
            config.sampleRate = node.text().as_int(config.sampleRate);
        }
        if (auto node = settings.child("impedance")) {
            config.impedance = node.text().as_bool(config.impedance);
        }
        if (auto node = settings.child("nativeformat")) {
            config.nativeFormat = node.text().as_bool(config.nativeFormat);
        }
    }

    return config;
}

void AmpServerConfig::saveToFile(const std::string& filename) const {
    pugi::xml_document doc;

    // Create ampserver section
    auto ampserver = doc.append_child("ampserver");
    ampserver.append_child("address").text().set(serverAddress.c_str());
    ampserver.append_child("commandport").text().set(commandPort);
    ampserver.append_child("notificationport").text().set(notificationPort);
    ampserver.append_child("dataport").text().set(dataPort);

    // Create settings section
    auto settings = doc.append_child("settings");
    settings.append_child("amplifierid").text().set(amplifierId);
    settings.append_child("samplingrate").text().set(sampleRate);
    settings.append_child("impedance").text().set(impedance);
    settings.append_child("nativeformat").text().set(nativeFormat);

    if (!doc.save_file(filename.c_str())) {
        throw ConfigError("Could not write to config file: " + filename);
    }
}

} // namespace egiamp
