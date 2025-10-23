// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <iostream>
#include <memory>
#include <string>
#include <chrono>
#include <thread>
#include <grpcpp/grpcpp.h>
#include "benchmark.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using benchmark::BenchmarkService;
using benchmark::EchoRequest;
using benchmark::EchoResponse;
using benchmark::AddRequest;
using benchmark::AddResponse;
using benchmark::VoidRequest;
using benchmark::VoidResponse;

// Server implementation
class BenchmarkServiceImpl final : public BenchmarkService::Service {
public:
    Status EchoMessage(ServerContext* context, const EchoRequest* request,
                      EchoResponse* response) override {
        response->set_result("Echo: " + request->message());
        return Status::OK;
    }

    Status Add(ServerContext* context, const AddRequest* request,
               AddResponse* response) override {
        response->set_result(request->a() + request->b());
        return Status::OK;
    }

    Status VoidMethod(ServerContext* context, const VoidRequest* request,
                     VoidResponse* response) override {
        // Do nothing
        return Status::OK;
    }
};

// Client class
class BenchmarkClient {
public:
    BenchmarkClient(std::shared_ptr<Channel> channel)
        : stub_(BenchmarkService::NewStub(channel)) {}

    std::string EchoMessage(const std::string& message) {
        EchoRequest request;
        request.set_message(message);
        EchoResponse response;
        ClientContext context;

        Status status = stub_->EchoMessage(&context, request, &response);
        if (status.ok()) {
            return response.result();
        } else {
            return "Error: " + status.error_message();
        }
    }

    int Add(int a, int b) {
        AddRequest request;
        request.set_a(a);
        request.set_b(b);
        AddResponse response;
        ClientContext context;

        Status status = stub_->Add(&context, request, &response);
        if (status.ok()) {
            return response.result();
        } else {
            return -1;
        }
    }

    bool VoidMethod() {
        VoidRequest request;
        VoidResponse response;
        ClientContext context;

        Status status = stub_->VoidMethod(&context, request, &response);
        return status.ok();
    }

private:
    std::unique_ptr<BenchmarkService::Stub> stub_;
};

// Performance test functions
double testLatency(BenchmarkClient& client, const std::string& testType) {
    const int iterations = 1000;
    
    std::cout << "Testing " << testType << " latency with " << iterations << " iterations..." << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        std::string result = client.EchoMessage("test");
    }
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    double avgLatency = static_cast<double>(duration.count()) / iterations / 1000.0; // Convert to ms
    
    std::cout << testType << " Average latency: " << avgLatency << " ms" << std::endl;
    return avgLatency;
}

double testThroughput(BenchmarkClient& client, const std::string& testType) {
    const int iterations = 5000;
    
    std::cout << "Testing " << testType << " throughput with " << iterations << " iterations..." << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        int result = client.Add(1, 2);
    }
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    double throughput = static_cast<double>(iterations) / (duration.count() / 1000.0);
    
    std::cout << testType << " Throughput: " << throughput << " ops/sec" << std::endl;
    return throughput;
}

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

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "server") {
        // Run as server
        RunServer();
        return 0;
    }

    // Run as client (performance test)
    std::cout << "gRPC Performance Test Suite" << std::endl;
    std::cout << "==============================" << std::endl;
    std::cout << std::endl;

    // Connect to server
    std::string server_address("localhost:50051");
    BenchmarkClient client(grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()));

    // Wait a bit for connection
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Test latency
    double latency = testLatency(client, "gRPC");
    std::cout << std::endl;

    // Test throughput  
    double throughput = testThroughput(client, "gRPC");
    std::cout << std::endl;

    // Summary
    std::cout << "=== gRPC Test Summary ===" << std::endl;
    std::cout << "gRPC Average latency: " << latency << " ms" << std::endl;
    std::cout << "gRPC Throughput: " << throughput << " ops/sec" << std::endl;
    std::cout << std::endl;
    std::cout << "Note: Compare these results with SlotIPC performance data." << std::endl;

    return 0;
} 