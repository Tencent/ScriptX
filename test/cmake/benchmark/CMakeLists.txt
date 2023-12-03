# https://github.com/google/benchmark#installation

set(DEPENDENCY_BENCHMARK_BRANCH main)
if (NOT EXISTS ${SCRIPTX_TEST_GOOGLE_BENCHMARK})
    # Download and unpack benchmark at configure time
    configure_file(${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt.in benchmark-download/CMakeLists.txt)
    execute_process(COMMAND
            ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
            RESULT_VARIABLE result
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/benchmark-download
            )
    if (result)
        message(FATAL_ERROR "CMake step for benchmark failed: ${result}")
    endif ()

    execute_process(COMMAND
            ${CMAKE_COMMAND} --build .
            RESULT_VARIABLE result
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/benchmark-download
            )
    if (result)
        message(FATAL_ERROR "Build step for benchmark failed: ${result}")
    endif ()
elseif (("${SCRIPTX_TEST_FORCE_UPDATE_DEPS}" STREQUAL ON) OR ("$ENV{SCRIPTX_TEST_FORCE_UPDATE_DEPS}" STREQUAL ON))
    message(STATUS "update SCRIPTX_TEST_GOOGLE_BENCHMARK ${SCRIPTX_TEST_GOOGLE_BENCHMARK}")
    execute_process(COMMAND
            git fetch
            WORKING_DIRECTORY ${SCRIPTX_TEST_GOOGLE_BENCHMARK}
            )
    execute_process(COMMAND
            git checkout --force origin/${DEPENDENCY_BENCHNARK_BRANCH}
            WORKING_DIRECTORY ${SCRIPTX_TEST_GOOGLE_TEST}
            )
endif ()

# Add benchmark directly to our build. This defines
# the gtest and gtest_main targets.
add_subdirectory(${SCRIPTX_TEST_GOOGLE_BENCHMARK}
        ${CMAKE_CURRENT_BINARY_DIR}/benchmark-build
        EXCLUDE_FROM_ALL)
