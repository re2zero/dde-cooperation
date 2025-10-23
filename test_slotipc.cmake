cmake_minimum_required(VERSION 3.13)
project(SlotIPCTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Qt
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Network)

# SlotIPC configuration
if(NOT TARGET slotipc)
    # Module subdirectory
    set(SLOTIPC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/infrastructure/slotipc")
    
    # set build as share library
    if(MSVC)
        set(BUILD_SHARED_LIBS OFF)
    else()
        set(BUILD_SHARED_LIBS ON)
    endif()
    
    SET(QT_DESIRED_VERSION ${QT_VERSION_MAJOR})
    
    # Module subdirectory
    add_subdirectory("${SLOTIPC_DIR}" slotipc_build)
    include_directories(${SLOTIPC_DIR}/include)
endif()

# Create test executable
set(CMAKE_AUTOMOC ON)

add_executable(slotipc_performance_test
    slotipc_performance_test.cpp
)

target_link_libraries(slotipc_performance_test
    slotipc
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
) 