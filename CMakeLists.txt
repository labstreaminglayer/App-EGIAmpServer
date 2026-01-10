cmake_minimum_required(VERSION 3.28)

# Handle newer CMake policies gracefully
if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()

project(EGIAmpServer
    DESCRIPTION "EGI AmpServer to LSL bridge"
    HOMEPAGE_URL "https://github.com/labstreaminglayer/App-EGIAmpServer/"
    LANGUAGES CXX
    VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# Build Options
# =============================================================================
option(EGIAMP_BUILD_GUI "Build the GUI application" ON)
option(EGIAMP_BUILD_CLI "Build the CLI application" ON)
option(EGIAMP_BUILD_MOCK "Build the mock EGI Amp device for testing" ON)

# =============================================================================
# liblsl Dependency
# =============================================================================
# By default, liblsl is fetched automatically from GitHub.
# To use a pre-installed liblsl, set LSL_INSTALL_ROOT.
set(LSL_INSTALL_ROOT "" CACHE PATH "Path to installed liblsl (optional)")
set(LSL_FETCH_REF "v1.17.0" CACHE STRING "liblsl version to fetch from GitHub")

if(LSL_INSTALL_ROOT)
    # Use pre-installed liblsl
    find_package(LSL REQUIRED
        HINTS "${LSL_INSTALL_ROOT}"
        PATH_SUFFIXES share/LSL lib/cmake/LSL Frameworks/lsl.framework/Resources/CMake
    )
    message(STATUS "Using installed liblsl: ${LSL_DIR}")
    include("${LSL_DIR}/LSLCMake.cmake")
else()
    # Fetch liblsl from GitHub
    message(STATUS "Fetching liblsl ${LSL_FETCH_REF} from GitHub...")
    include(FetchContent)
    set(LSL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(liblsl
        GIT_REPOSITORY https://github.com/sccn/liblsl.git
        GIT_TAG ${LSL_FETCH_REF}
        GIT_SHALLOW ON
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(liblsl)
    if(NOT TARGET LSL::lsl)
        add_library(LSL::lsl ALIAS lsl)
    endif()
    include("${liblsl_SOURCE_DIR}/cmake/LSLCMake.cmake")
endif()

# =============================================================================
# Qt6 (for GUI build only)
# =============================================================================
if(EGIAMP_BUILD_GUI)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)
    find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

    # macOS: Work around deprecated AGL framework issue in some Qt6 builds
    # AGL was removed from modern macOS SDKs (Apple Silicon)
    if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
        get_target_property(_wrap_gl_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
        if(_wrap_gl_libs)
            # Remove AGL from the list (it's a full path like /System/Library/Frameworks/AGL.framework)
            list(FILTER _wrap_gl_libs EXCLUDE REGEX ".*AGL.*")
            set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES INTERFACE_LINK_LIBRARIES "${_wrap_gl_libs}")
        endif()
    endif()
endif()

# =============================================================================
# Common dependencies
# =============================================================================
find_package(Threads REQUIRED)

# Standalone Asio (header-only networking library)
include(FetchContent)
FetchContent_Declare(asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-30-2
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(asio)
add_library(asio INTERFACE)
target_include_directories(asio INTERFACE "${asio_SOURCE_DIR}/asio/include")
target_compile_definitions(asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
if(WIN32)
    target_compile_definitions(asio INTERFACE _WIN32_WINNT=0x0601)  # Target Windows 7+
    target_link_libraries(asio INTERFACE ws2_32 mswsock)
endif()
target_link_libraries(asio INTERFACE Threads::Threads)

# pugixml - use the one bundled with liblsl if available, otherwise fetch
if(TARGET pugixml OR TARGET pugixml-static)
    message(STATUS "Using pugixml from liblsl")
else()
    FetchContent_Declare(pugixml
        GIT_REPOSITORY https://github.com/zeux/pugixml.git
        GIT_TAG v1.15
        GIT_SHALLOW ON
    )
    FetchContent_MakeAvailable(pugixml)
endif()

# =============================================================================
# RPATH Configuration (must be set before targets are created)
# =============================================================================
LSL_configure_rpath()

# =============================================================================
# Targets
# =============================================================================

# Core library (Qt-independent)
add_subdirectory(src/core)

# CLI application
if(EGIAMP_BUILD_CLI)
    add_subdirectory(src/cli)
endif()

# GUI application
if(EGIAMP_BUILD_GUI)
    add_subdirectory(src/gui)
endif()

# Mock EGI Amp device for testing
if(EGIAMP_BUILD_MOCK)
    add_subdirectory(mock)
endif()

# =============================================================================
# Installation
# =============================================================================
include(GNUInstallDirs)

# Determine install destinations based on platform
if(WIN32)
    set(INSTALL_BINDIR ".")
    set(INSTALL_LIBDIR ".")
    set(INSTALL_DATADIR ".")
elseif(APPLE)
    set(INSTALL_BINDIR ".")
    set(INSTALL_LIBDIR ".")
    set(INSTALL_DATADIR ".")
else()
    # Linux: use standard FHS layout
    set(INSTALL_BINDIR "${CMAKE_INSTALL_BINDIR}")
    set(INSTALL_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
    set(INSTALL_DATADIR "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}")
endif()

# Install CLI
if(EGIAMP_BUILD_CLI)
    install(TARGETS EGIAmpServerCLI
        RUNTIME DESTINATION "${INSTALL_BINDIR}"
    )
endif()

# Install GUI
if(EGIAMP_BUILD_GUI)
    install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION "${INSTALL_BINDIR}"
        BUNDLE DESTINATION "${INSTALL_BINDIR}"
    )
endif()

# Install auxiliary files
set(_install_dest "${INSTALL_DATADIR}")
if(APPLE AND EGIAMP_BUILD_GUI)
    set(_install_dest "${INSTALL_BINDIR}/${PROJECT_NAME}.app/Contents/MacOS")
endif()
install(FILES ampserver_config.cfg DESTINATION "${_install_dest}")

# =============================================================================
# Bundle liblsl with the application
# =============================================================================
LSL_install_liblsl(
    DESTINATION "${INSTALL_LIBDIR}"
    FRAMEWORK_DESTINATION "${INSTALL_BINDIR}/${PROJECT_NAME}.app/Contents/Frameworks"
)

# =============================================================================
# Qt Deployment
# =============================================================================
if(EGIAMP_BUILD_GUI)
    LSL_deploy_qt(TARGET "${PROJECT_NAME}" DESTINATION "${INSTALL_BINDIR}")
endif()

# =============================================================================
# MinGW Runtime Deployment
# =============================================================================
LSL_install_mingw_runtime(DESTINATION "${INSTALL_BINDIR}")

# =============================================================================
# macOS: Code signing
# =============================================================================
if(APPLE AND EGIAMP_BUILD_GUI)
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.labstreaminglayer.EGIAmpServer")
    set(MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

    LSL_codesign(
        TARGET "${PROJECT_NAME}"
        DESTINATION "${INSTALL_BINDIR}"
        ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/app.entitlements"
        BUNDLE
    )
endif()

# Post-build commands for copying config file are in subdirectory CMakeLists.txt

# =============================================================================
# CPack Configuration
# =============================================================================
LSL_get_target_arch()
LSL_get_os_name()

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Labstreaminglayer")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}-${LSL_OS}_${LSL_ARCH}")
set(CPACK_STRIP_FILES ON)

if(WIN32)
    set(CPACK_GENERATOR ZIP)
elseif(APPLE)
    set(CPACK_GENERATOR TGZ)
else()
    set(CPACK_GENERATOR DEB TGZ)
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "LabStreamingLayer Developers")
    set(CPACK_DEBIAN_PACKAGE_SECTION "science")
    set(CPACK_DEBIAN_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}.deb")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
    if(EGIAMP_BUILD_GUI)
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6widgets6")
    endif()
endif()

include(CPack)
