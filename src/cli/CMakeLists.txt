add_executable(EGIAmpServerCLI
    main.cpp
)

target_link_libraries(EGIAmpServerCLI PRIVATE
    egiamp_core
)

# Post-build: copy config file
add_custom_command(TARGET EGIAmpServerCLI POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/ampserver_config.cfg"
        "$<TARGET_FILE_DIR:EGIAmpServerCLI>"
    COMMENT "Copying config file to CLI build directory"
)

# Windows: copy liblsl.dll
if(WIN32)
    add_custom_command(TARGET EGIAmpServerCLI POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:LSL::lsl>
            $<TARGET_FILE_DIR:EGIAmpServerCLI>
        COMMENT "Copying liblsl.dll to CLI build directory"
    )
endif()
