# =========================================================================
# Embedded resources (tiling XMLs + montage .sfp files)
# =========================================================================
set(RESOURCE_DIR "${CMAKE_SOURCE_DIR}/resources")
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

set(RESOURCE_FILES
    "${RESOURCE_DIR}/tiling/HydroCel GSN 32 1.0.xml"
    "${RESOURCE_DIR}/tiling/HydroCel GSN 64 1.0.xml"
    "${RESOURCE_DIR}/tiling/HydroCel GSN 128 1.0.xml"
    "${RESOURCE_DIR}/tiling/HydroCel GSN 256 1.0.xml"
    "${RESOURCE_DIR}/montages/GSN-HydroCel-32.sfp"
    "${RESOURCE_DIR}/montages/GSN-HydroCel-64.sfp"
    "${RESOURCE_DIR}/montages/GSN-HydroCel-65.sfp"
    "${RESOURCE_DIR}/montages/GSN-HydroCel-128.sfp"
    "${RESOURCE_DIR}/montages/GSN-HydroCel-129.sfp"
    "${RESOURCE_DIR}/montages/GSN-HydroCel-256.sfp"
    "${RESOURCE_DIR}/montages/GSN-HydroCel-257.sfp"
)

# Write manifest file at configure time (one path per line, avoids shell quoting issues)
set(RESOURCE_MANIFEST "${GENERATED_DIR}/resource_manifest.txt")
string(REPLACE ";" "\n" MANIFEST_CONTENT "${RESOURCE_FILES}")
file(WRITE "${RESOURCE_MANIFEST}" "${MANIFEST_CONTENT}\n")

add_custom_command(
    OUTPUT
        "${GENERATED_DIR}/egiamp/EmbeddedResources.h"
        "${GENERATED_DIR}/EmbeddedResources.cpp"
    COMMAND ${CMAKE_COMMAND}
        "-DMANIFEST=${RESOURCE_MANIFEST}"
        "-DOUTPUT_DIR=${GENERATED_DIR}"
        -P "${CMAKE_SOURCE_DIR}/cmake/embed_resources.cmake"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/cmake/embed_resources.cmake"
        ${RESOURCE_FILES}
    COMMENT "Generating embedded resources"
)

# =========================================================================
# Core library
# =========================================================================
add_library(egiamp_core STATIC
    src/AmpServerConfig.cpp
    src/AmpServerConnection.cpp
    src/ElectrodePositions.cpp
    src/ImpedanceMeasurement.cpp
    src/LSLStreamer.cpp
    src/EGIAmpClient.cpp
    src/SensorLayout.cpp
    "${GENERATED_DIR}/EmbeddedResources.cpp"
)

target_include_directories(egiamp_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${GENERATED_DIR}>
    $<INSTALL_INTERFACE:include>
)

# Determine which pugixml target to use
if(TARGET pugixml-static)
    set(PUGIXML_TARGET pugixml-static)
elseif(TARGET pugixml)
    set(PUGIXML_TARGET pugixml)
else()
    message(FATAL_ERROR "pugixml target not found")
endif()

target_link_libraries(egiamp_core
    PUBLIC
        LSL::lsl
        asio
        Threads::Threads
    PRIVATE
        ${PUGIXML_TARGET}
)
