if (NOT EXISTS "${SCRIPTX_LIBS}/README")

    message(STATUS "SCRIPTX_LIBS ${SCRIPTX_LIBS} is empty. "
            "Downloading test libs including V8, JavaScriptCore, Lua, etc.")

    configure_file(${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt.in ScriptXLibsDownload/CMakeLists.txt)

    execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
            RESULT_VARIABLE result
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/ScriptXLibsDownload)

    if (result)
        message(FATAL_ERROR "CMake step for download ScriptXLibs failed: ${result}")
    endif ()

    execute_process(COMMAND ${CMAKE_COMMAND} --build .
            RESULT_VARIABLE result
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/ScriptXLibsDownload)

    if (result)
        message(FATAL_ERROR "Build step for download ScriptXLibs failed: ${result}")
    endif ()
endif ()
