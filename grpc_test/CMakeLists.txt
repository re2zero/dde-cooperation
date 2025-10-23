cmake_minimum_required(VERSION 3.13)
project(gRPCBenchmark)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC REQUIRED grpc++)
pkg_check_modules(PROTOBUF REQUIRED protobuf)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Source files
set(SOURCES
    grpc_benchmark.cpp
    benchmark.pb.cc
    benchmark.grpc.pb.cc
)

# Create executable
add_executable(grpc_benchmark ${SOURCES})

# Link libraries
target_link_libraries(grpc_benchmark
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
)

# Compiler flags
target_compile_options(grpc_benchmark PRIVATE
    ${GRPC_CFLAGS_OTHER}
    ${PROTOBUF_CFLAGS_OTHER}
) 