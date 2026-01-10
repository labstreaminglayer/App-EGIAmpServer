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
# Options
# =============================================================================
option(EGIAMP_BUILD_GUI "Build the GUI application" ON)
option(EGIAMP_BUILD_CLI "Build the CLI application" ON)
option(EGIAMP_BUILD_MOCK "Build the mock EGI Amp device for testing" ON)

# LSL dependency options
set(LSL_SOURCE_DIR "" CACHE PATH "Path to liblsl source for parallel development (highest priority)")
set(LSL_INSTALL_ROOT "" CACHE PATH "Path to installed liblsl (second priority)")
option(LSL_FETCH_IF_MISSING "Fetch liblsl from GitHub if not found locally" ON)
# TODO: Change back to version tag (e.g., "v1.16.2") once apple_framework branch is merged
set(LSL_FETCH_REF "cboulay/apple_framework" CACHE STRING "liblsl git ref to fetch (tag, branch, or commit)")

# =============================================================================
# Find/Fetch liblsl
# Priority: 1) LSL_SOURCE_DIR, 2) LSL_INSTALL_ROOT, 3) System install, 4) FetchContent
# =============================================================================
if(LSL_SOURCE_DIR)
    # Priority 1: Build from local source (parallel development)
    if(NOT EXISTS "${LSL_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "LSL_SOURCE_DIR set to '${LSL_SOURCE_DIR}' but no CMakeLists.txt found there")
    endif()
    message(STATUS "Using local liblsl source: ${LSL_SOURCE_DIR}")
    add_subdirectory("${LSL_SOURCE_DIR}" liblsl_bin EXCLUDE_FROM_ALL)
    if(NOT TARGET LSL::lsl)
        add_library(LSL::lsl ALIAS lsl)
    endif()
    set(LSL_BUILT_FROM_SOURCE TRUE)
    include("${LSL_SOURCE_DIR}/cmake/LSLCMake.cmake")
else()
    # Priority 2 & 3: Try to find installed version
    set(_lsl_hints)
    if(LSL_INSTALL_ROOT)
        list(APPEND _lsl_hints "${LSL_INSTALL_ROOT}")
    endif()
    # Common development layout hints (including CLion cmake-build-* directories)
    string(TOLOWER "${CMAKE_BUILD_TYPE}" _build_type_lower)
    if(NOT _build_type_lower)
        set(_build_type_lower "release")
    endif()
    if(MSVC)
        set(_clion_build_dir "cmake-build-${_build_type_lower}-visual-studio")
    else()
        set(_clion_build_dir "cmake-build-${_build_type_lower}")
    endif()
    foreach(_root IN ITEMS "../liblsl" "../../LSL/liblsl")
        foreach(_build IN ITEMS "build" "${_clion_build_dir}")
            list(APPEND _lsl_hints "${CMAKE_CURRENT_LIST_DIR}/${_root}/${_build}/install")
        endforeach()
    endforeach()
    # Also check Visual Studio layout
    list(APPEND _lsl_hints "${CMAKE_CURRENT_LIST_DIR}/../../LSL/liblsl/out/install/x64-Release")

    set(_lsl_suffixes
        share/LSL
        lib/cmake/LSL
        cmake
        Frameworks/lsl.framework/Resources/CMake
    )

    # First try: Search only in hints (prefer local builds over system)
    find_package(LSL QUIET
        HINTS ${_lsl_hints}
        PATH_SUFFIXES ${_lsl_suffixes}
        NO_DEFAULT_PATH
    )

    # Second try: Search system paths if not found in hints
    if(NOT LSL_FOUND)
        find_package(LSL QUIET
            PATH_SUFFIXES ${_lsl_suffixes}
        )
    endif()

    if(LSL_FOUND)
        message(STATUS "Found installed liblsl: ${LSL_DIR}")
        set(LSL_BUILT_FROM_SOURCE FALSE)
        include("${LSL_DIR}/LSLCMake.cmake")
    elseif(LSL_FETCH_IF_MISSING)
        # Priority 4: Fetch from GitHub
        message(STATUS "liblsl not found locally, fetching ${LSL_FETCH_REF} from GitHub...")
        include(FetchContent)
        FetchContent_Declare(liblsl
            GIT_REPOSITORY https://github.com/sccn/liblsl.git
            GIT_TAG ${LSL_FETCH_REF}
            GIT_SHALLOW ON
            EXCLUDE_FROM_ALL
        )
        set(LSL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(LSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(liblsl)
        if(NOT TARGET LSL::lsl)
            add_library(LSL::lsl ALIAS lsl)
        endif()
        set(LSL_BUILT_FROM_SOURCE TRUE)
        include("${liblsl_SOURCE_DIR}/cmake/LSLCMake.cmake")
        message(STATUS "liblsl fetched and configured")
    else()
        message(FATAL_ERROR
            "liblsl not found. Options:\n"
            "  1. Set LSL_SOURCE_DIR to liblsl source directory\n"
            "  2. Set LSL_INSTALL_ROOT to installed liblsl location\n"
            "  3. Install liblsl system-wide\n"
            "  4. Enable LSL_FETCH_IF_MISSING=ON to auto-fetch from GitHub"
        )
    endif()
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
