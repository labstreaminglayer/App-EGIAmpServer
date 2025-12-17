add_executable(${PROJECT_NAME} MACOSX_BUNDLE WIN32
    main.cpp
    EGIAmpWindow.cpp
    EGIAmpWindow.h
    EGIAmpWindow.ui
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    egiamp_core
    Qt6::Core
    Qt6::Widgets
)

# Post-build: copy config file
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/ampserver_config.cfg"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMENT "Copying config file to build directory"
)

# Platform-specific post-build commands
if(WIN32)
    # Copy liblsl.dll
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:LSL::lsl>
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMENT "Copying liblsl.dll to build directory"
    )
    # Run windeployqt
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                "$<TARGET_FILE:${PROJECT_NAME}>"
            COMMENT "Running windeployqt"
        )
    endif()

elseif(APPLE)
    # Copy liblsl to Frameworks
    set(BUNDLE_FRAMEWORKS_DIR "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>/Contents/Frameworks")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_FRAMEWORKS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:LSL::lsl>
            "${BUNDLE_FRAMEWORKS_DIR}/"
        COMMENT "Copying liblsl to app bundle Frameworks"
    )
endif()
