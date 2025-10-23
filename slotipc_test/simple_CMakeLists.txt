cmake_minimum_required(VERSION 3.13)
project(SimpleSlotIPCTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Qt
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Network)

# Include SlotIPC
set(SLOTIPC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src/infrastructure/slotipc)
set(SLOTIPC_BUILD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../build/src/infrastructure/slotipc)
include_directories(${SLOTIPC_DIR}/include)

# Create test executable
set(CMAKE_AUTOMOC ON)

add_executable(simple_test
    simple_test.cpp
)

target_link_libraries(simple_test
    ${SLOTIPC_BUILD_DIR}/libslotipc.so.0.1.0
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
)
