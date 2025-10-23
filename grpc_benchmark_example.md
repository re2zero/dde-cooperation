# gRPC基准测试实现示例

## 概述

为了与SlotIPC进行公平的性能对比，需要实现一个简单的gRPC服务。以下是实现步骤和示例代码。

## 1. 定义protobuf文件

创建 `benchmark.proto`：

```protobuf
syntax = "proto3";

package benchmark;

service BenchmarkService {
    rpc EchoMessage(EchoRequest) returns (EchoResponse);
    rpc VoidMethod(VoidRequest) returns (VoidResponse);
}

message EchoRequest {
    string message = 1;
}

message EchoResponse {
    string result = 1;
}

message VoidRequest {
}

message VoidResponse {
}
```

## 2. 生成gRPC代码

```bash
# 安装gRPC和protobuf
sudo apt install libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc

# 生成代码
protoc --grpc_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` benchmark.proto
protoc --cpp_out=. benchmark.proto
```

## 3. 实现gRPC服务端

创建 `grpc_server.cpp`：

```cpp
#include <grpcpp/grpcpp.h>
#include "benchmark.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

class BenchmarkServiceImpl final : public benchmark::BenchmarkService::Service {
public:
    Status EchoMessage(ServerContext* context, 
                      const benchmark::EchoRequest* request,
                      benchmark::EchoResponse* response) override {
        response->set_result("Echo: " + request->message());
        return Status::OK;
    }
    
    Status VoidMethod(ServerContext* context,
                     const benchmark::VoidRequest* request,
                     benchmark::VoidResponse* response) override {
        return Status::OK;
    }
};

void RunServer() {
    std::string server_address("localhost:50051");
    BenchmarkServiceImpl service;
    
    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "gRPC Server listening on " << server_address << std::endl;
    server->Wait();
}

int main() {
    RunServer();
    return 0;
}
```

## 4. 实现gRPC客户端基准测试

创建 `grpc_benchmark.cpp`：

```cpp
#include <grpcpp/grpcpp.h>
#include <chrono>
#include <iostream>
#include "benchmark.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

class BenchmarkClient {
public:
    BenchmarkClient(std::shared_ptr<Channel> channel)
        : stub_(benchmark::BenchmarkService::NewStub(channel)) {}
    
    void TestLatency() {
        const int iterations = 1000;
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < iterations; ++i) {
            benchmark::EchoRequest request;
            request.set_message("test");
            benchmark::EchoResponse response;
            ClientContext context;
            
            Status status = stub_->EchoMessage(&context, request, &response);
            if (!status.ok()) {
                std::cout << "gRPC call failed" << std::endl;
                return;
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        double avgLatency = static_cast<double>(duration.count()) / iterations / 1000.0;
        
        std::cout << "gRPC Average latency: " << avgLatency << " ms" << std::endl;
    }
    
    void TestThroughput() {
        const int iterations = 5000;
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < iterations; ++i) {
            benchmark::VoidRequest request;
            benchmark::VoidResponse response;
            ClientContext context;
            
            Status status = stub_->VoidMethod(&context, request, &response);
            if (!status.ok()) {
                std::cout << "gRPC call failed" << std::endl;
                return;
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        double throughput = static_cast<double>(iterations) / (duration.count() / 1000.0);
        
        std::cout << "gRPC Throughput: " << throughput << " ops/sec" << std::endl;
    }

private:
    std::unique_ptr<benchmark::BenchmarkService::Stub> stub_;
};

int main() {
    std::string server_address("localhost:50051");
    auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
    BenchmarkClient client(channel);
    
    std::cout << "=== gRPC Performance Test ===" << std::endl;
    std::cout << "Testing gRPC latency with 1000 iterations..." << std::endl;
    client.TestLatency();
    
    std::cout << "Testing gRPC throughput with 5000 iterations..." << std::endl;
    client.TestThroughput();
    
    return 0;
}
```

## 5. 编译gRPC测试程序

创建 `CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.13)
project(gRPC_Benchmark)

set(CMAKE_CXX_STANDARD 17)

# Find packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC REQUIRED grpc++)
pkg_check_modules(PROTOBUF REQUIRED protobuf)

# Generated files
set(PROTO_SRCS "${CMAKE_CURRENT_BINARY_DIR}/benchmark.pb.cc")
set(PROTO_HDRS "${CMAKE_CURRENT_BINARY_DIR}/benchmark.pb.h")
set(GRPC_SRCS "${CMAKE_CURRENT_BINARY_DIR}/benchmark.grpc.pb.cc")
set(GRPC_HDRS "${CMAKE_CURRENT_BINARY_DIR}/benchmark.grpc.pb.h")

# Generate protobuf and gRPC files
add_custom_command(
    OUTPUT "${PROTO_SRCS}" "${PROTO_HDRS}" "${GRPC_SRCS}" "${GRPC_HDRS}"
    COMMAND protoc
    ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
         --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
         -I "${CMAKE_CURRENT_SOURCE_DIR}"
         --plugin=protoc-gen-grpc=`which grpc_cpp_plugin`
         "${CMAKE_CURRENT_SOURCE_DIR}/benchmark.proto"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/benchmark.proto"
)

# Server executable
add_executable(grpc_server
    grpc_server.cpp
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)

target_link_libraries(grpc_server
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
)

# Benchmark client executable
add_executable(grpc_benchmark
    grpc_benchmark.cpp
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)

target_link_libraries(grpc_benchmark
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
)

target_include_directories(grpc_server PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(grpc_benchmark PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
```

## 6. 运行gRPC基准测试

```bash
# 编译
mkdir build && cd build
cmake ..
make

# 运行服务端（在一个终端中）
./grpc_server

# 运行基准测试（在另一个终端中）
./grpc_benchmark
```

## 7. 预期输出

```bash
=== gRPC Performance Test ===
Testing gRPC latency with 1000 iterations...
gRPC Average latency: [实际测试结果] ms
Testing gRPC throughput with 5000 iterations...
gRPC Throughput: [实际测试结果] ops/sec
```

## 注意事项

1. **环境一致性**：确保gRPC测试与SlotIPC测试在相同环境下运行
2. **网络配置**：使用本地回环接口避免网络延迟影响
3. **资源使用**：监控CPU和内存使用情况
4. **多次测试**：运行多次取平均值以获得稳定结果

通过实现这个gRPC基准测试，可以获得真实的性能对比数据，为技术调研报告提供客观的比较基准。 