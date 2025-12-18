cmake_minimum_required(VERSION 3.14)
project(MockAmpServer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(Threads REQUIRED)

# Include FetchContent for asio
include(FetchContent)
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-28-0
)
FetchContent_MakeAvailable(asio)

# Create the mock server executable
add_executable(MockAmpServer
    src/main.cpp
    src/MockAmplifier.cpp
    src/CommandHandler.cpp
    src/NotificationHandler.cpp
    src/DataStreamGenerator.cpp
    src/ECIHandler.cpp
)

target_include_directories(MockAmpServer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${asio_SOURCE_DIR}/asio/include
)

target_compile_definitions(MockAmpServer PRIVATE
    ASIO_STANDALONE
    ASIO_NO_DEPRECATED
)

target_link_libraries(MockAmpServer PRIVATE Threads::Threads)

# Platform-specific
if(APPLE)
    target_link_libraries(MockAmpServer PRIVATE "-framework CoreFoundation")
endif()

# Install
install(TARGETS MockAmpServer RUNTIME DESTINATION bin)
