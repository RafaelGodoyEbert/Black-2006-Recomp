cmake_minimum_required(VERSION 3.20)
project(physics_timing_test C)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(hook "${CMAKE_CURRENT_BINARY_DIR}/physics_timing.extracted.inc")
add_custom_command(OUTPUT "${hook}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/extract_hook.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/recomp_manual.c" "${hook}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/recomp_manual.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/extract_hook.py"
    VERBATIM)
add_executable(physics-timing-test test_physics.c "${hook}")
target_include_directories(physics-timing-test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
if(MSVC)
    target_compile_options(physics-timing-test PRIVATE /W4 /UNDEBUG /fp:strict)
else()
    target_link_libraries(physics-timing-test PRIVATE m)
endif()
enable_testing()
add_test(NAME physics-timing COMMAND physics-timing-test)
add_test(NAME physics-generator COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_generator.py")
