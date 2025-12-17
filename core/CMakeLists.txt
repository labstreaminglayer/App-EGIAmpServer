add_library(egiamp_core STATIC
    src/AmpServerConfig.cpp
    src/AmpServerConnection.cpp
    src/LSLStreamer.cpp
    src/EGIAmpClient.cpp
)

target_include_directories(egiamp_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
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
