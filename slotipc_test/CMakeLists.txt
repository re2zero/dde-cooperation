cmake_minimum_required(VERSION 3.13)
project(SlotIPCTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Qt6 specifically
find_package(Qt6 REQUIRED COMPONENTS Core Network)
set(QT_VERSION_MAJOR 6)

# Include SlotIPC
set(SLOTIPC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src/infrastructure/slotipc)
set(SLOTIPC_BUILD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../build/src/infrastructure/slotipc)
include_directories(${SLOTIPC_DIR}/include)

# Create test executables
set(CMAKE_AUTOMOC ON)

# Simple functional test
add_executable(simple_test
    simple_test.cpp
)

target_link_libraries(simple_test
    ${SLOTIPC_BUILD_DIR}/libslotipc.so.0.1.0
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
)

# Performance test
add_executable(slotipc_performance_test
    slotipc_performance_test.cpp
)

target_link_libraries(slotipc_performance_test
    ${SLOTIPC_BUILD_DIR}/libslotipc.so.0.1.0
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
)
