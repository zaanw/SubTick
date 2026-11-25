cmake_minimum_required(VERSION 3.21)
project(SubTickCoreTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_executable(SubTickTimelineTests
    timeline_tests.cpp
    ../src/engine/timeline.cpp
)
target_include_directories(SubTickTimelineTests PRIVATE ../src)
add_test(NAME SubTickTimelineTests COMMAND SubTickTimelineTests)

add_executable(SubTickFpsInvarianceTests
    fps_invariance_tests.cpp
    ../src/engine/timeline.cpp
)
target_include_directories(SubTickFpsInvarianceTests PRIVATE ../src)
add_test(NAME SubTickFpsInvarianceTests COMMAND SubTickFpsInvarianceTests)
