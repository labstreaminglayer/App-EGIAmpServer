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

# LSL dependency options
set(LSL_SOURCE_DIR "" CACHE PATH "Path to liblsl source for parallel development (highest priority)")
set(LSL_INSTALL_ROOT "" CACHE PATH "Path to installed liblsl (second priority)")
option(LSL_FETCH_IF_MISSING "Fetch liblsl from GitHub if not found locally" ON)
set(LSL_FETCH_VERSION "1.16.2" CACHE STRING "liblsl version to fetch from GitHub")

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
else()
    # Priority 2 & 3: Try to find installed version
    set(_lsl_hints)
    if(LSL_INSTALL_ROOT)
        list(APPEND _lsl_hints "${LSL_INSTALL_ROOT}")
    endif()
    # Common development layout hints
    list(APPEND _lsl_hints
        "${CMAKE_CURRENT_LIST_DIR}/../../LSL/liblsl/build/install"
        "${CMAKE_CURRENT_LIST_DIR}/../../LSL/liblsl/out/install/x64-Release"
        "${CMAKE_CURRENT_LIST_DIR}/../liblsl/build/install"
    )

    find_package(LSL QUIET
        HINTS ${_lsl_hints}
        PATH_SUFFIXES share/LSL lib/cmake/LSL cmake
    )

    if(LSL_FOUND)
        message(STATUS "Found installed liblsl: ${LSL_DIR}")
        set(LSL_BUILT_FROM_SOURCE FALSE)
    elseif(LSL_FETCH_IF_MISSING)
        # Priority 4: Fetch from GitHub
        message(STATUS "liblsl not found locally, fetching v${LSL_FETCH_VERSION} from GitHub...")
        include(FetchContent)
        FetchContent_Declare(liblsl
            GIT_REPOSITORY https://github.com/sccn/liblsl.git
            GIT_TAG v${LSL_FETCH_VERSION}
            GIT_SHALLOW ON
        )
        set(LSL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(LSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(liblsl)
        if(NOT TARGET LSL::lsl)
            add_library(LSL::lsl ALIAS lsl)
        endif()
        set(LSL_BUILT_FROM_SOURCE TRUE)
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
# Targets
# =============================================================================

# Core library (Qt-independent)
add_subdirectory(core)

# CLI application
if(EGIAMP_BUILD_CLI)
    add_subdirectory(cli)
endif()

# GUI application
if(EGIAMP_BUILD_GUI)
    add_subdirectory(gui)
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

# Set RPATH for finding bundled liblsl
if(APPLE)
    set(CMAKE_INSTALL_RPATH "@executable_path;@executable_path/../Frameworks")
elseif(UNIX AND NOT ANDROID)
    set(CMAKE_INSTALL_RPATH "$ORIGIN;$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

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
# Bundle liblsl with the application (install rules only - post-build in subdirs)
# =============================================================================
if(WIN32)
    install(FILES $<TARGET_FILE:LSL::lsl> DESTINATION "${INSTALL_BINDIR}")
elseif(APPLE AND EGIAMP_BUILD_GUI)
    install(FILES $<TARGET_FILE:LSL::lsl>
        DESTINATION "${INSTALL_BINDIR}/${PROJECT_NAME}.app/Contents/Frameworks"
    )
else()
    # Linux
    install(FILES $<TARGET_FILE:LSL::lsl> DESTINATION "${INSTALL_LIBDIR}")
endif()

# =============================================================================
# Qt Deployment (install rules only - post-build handled in gui/CMakeLists.txt)
# =============================================================================
if(EGIAMP_BUILD_GUI)
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)

    if(WIN32)
        find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}")
        if(WINDEPLOYQT_EXECUTABLE)
            install(CODE "
                message(STATUS \"Running windeployqt for installation...\")
                execute_process(
                    COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
                        --no-translations
                        --no-system-d3d-compiler
                        --no-opengl-sw
                        --no-compiler-runtime
                        --dir \"\${CMAKE_INSTALL_PREFIX}/${INSTALL_BINDIR}\"
                        \"\${CMAKE_INSTALL_PREFIX}/${INSTALL_BINDIR}/${PROJECT_NAME}.exe\"
                )
            ")
        endif()

    elseif(APPLE)
        find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${QT_BIN_DIR}")
        if(MACDEPLOYQT_EXECUTABLE)
            install(CODE "
                message(STATUS \"Running macdeployqt...\")
                execute_process(
                    COMMAND \"${MACDEPLOYQT_EXECUTABLE}\"
                        \"\${CMAKE_INSTALL_PREFIX}/${INSTALL_BINDIR}/${PROJECT_NAME}.app\"
                        -verbose=1
                        -always-overwrite
                )
            ")
        endif()
    endif()
endif()

# =============================================================================
# macOS: Code signing
# =============================================================================
if(APPLE AND EGIAMP_BUILD_GUI)
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.labstreaminglayer.EGIAmpServer")
    set(MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

    install(CODE "
        message(STATUS \"Running ad-hoc code signing...\")
        execute_process(
            COMMAND codesign --force --deep --sign -
                \"\${CMAKE_INSTALL_PREFIX}/${INSTALL_BINDIR}/${PROJECT_NAME}.app\"
        )
    ")
endif()

# Post-build commands for copying config file are in subdirectory CMakeLists.txt

# =============================================================================
# CPack Configuration
# =============================================================================
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)")
    set(PACKAGE_ARCH "arm64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)")
    set(PACKAGE_ARCH "amd64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86)")
    set(PACKAGE_ARCH "i386")
else()
    set(PACKAGE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
endif()

if(APPLE)
    set(PACKAGE_OS "macOS")
elseif(WIN32)
    set(PACKAGE_OS "Win")
else()
    set(PACKAGE_OS "Linux")
endif()

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Labstreaminglayer")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}-${PACKAGE_OS}_${PACKAGE_ARCH}")
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
