cmake_minimum_required(VERSION 3.20)
project(apu_timing_test C)
set(CMAKE_C_STANDARD 11)
set(XBOXRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/xboxrecomp"
    CACHE PATH "Patched pinned XboxRecomp runtime")
add_executable(apu-timing-test test_main.c)
target_include_directories(apu-timing-test PRIVATE "${XBOXRECOMP_DIR}/src/apu")
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(APU_TIMING_CORE_SOURCE "${XBOXRECOMP_DIR}/src/apu/apu_core.c" CACHE FILEPATH
    "Production APU core (override for a negative control)")
set(extracted "${CMAKE_CURRENT_BINARY_DIR}/throttle.inc")
add_custom_command(OUTPUT "${extracted}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/extract.py"
        "${APU_TIMING_CORE_SOURCE}" "${extracted}"
    DEPENDS "${APU_TIMING_CORE_SOURCE}" "${CMAKE_CURRENT_SOURCE_DIR}/extract.py"
    VERBATIM)
set_source_files_properties("${extracted}" PROPERTIES GENERATED TRUE HEADER_FILE_ONLY TRUE)
add_executable(apu-throttle-test test_throttle.c "${extracted}")
target_include_directories(apu-throttle-test PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}" "${XBOXRECOMP_DIR}/src/apu")
enable_testing()
add_test(NAME apu-timing COMMAND apu-timing-test)
add_test(NAME apu-throttle COMMAND apu-throttle-test)
set_tests_properties(apu-throttle PROPERTIES TIMEOUT 10)
