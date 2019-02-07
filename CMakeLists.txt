cmake_minimum_required(VERSION 3.5)

project(EGIAmpServer
	LANGUAGES CXX
	VERSION 1.13.0)

include(Findliblsl.cmake)

# GENERAL CONFIG #
set(META_PROJECT_DESCRIPTION "Stream from Philips Neuro devices via Lab Streaming Layer")

# THIRD PARTY LIBRARIES #
find_package(Qt5 COMPONENTS Widgets REQUIRED)
cmake_policy(SET CMP0074 NEW)  # <- Required to avoid conflict between BOOST_ROOT and Boost_ROOT
find_package(Boost COMPONENTS system REQUIRED)
find_package(Threads REQUIRED)

# Add executable target
add_executable(${PROJECT_NAME} MACOSX_BUNDLE WIN32
	main.cpp
	EGIAmpWindow.cpp
	EGIAmpWindow.h
	EGIAmpWindow.ui
)

target_link_libraries(${PROJECT_NAME}
	PRIVATE
	Qt5::Widgets
	LSL::lsl
	Boost::system
	Threads::Threads
)


target_compile_features(${PROJECT_NAME} PRIVATE cxx_auto_type)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_14)
target_compile_definitions(${PROJECT_NAME} PRIVATE BOOST BOOST_DATE_TIME_NO_LIB BOOST_REGEX_NO_LIB)

installLSLApp(${PROJECT_NAME})
installLSLAuxFiles(${PROJECT_NAME}
	ampserver_config.cfg
)

LSLGenerateCPackConfig()
