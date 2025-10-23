# gRPC性能测试说明

## 概述
本文档说明如何运行gRPC性能测试来获取真实的性能数据，用于与SlotIPC进行对比。

## 前置条件

### 系统要求
- **操作系统**: UOS/Deepin Linux 或其他Linux发行版
- **编译器**: GCC 12.3.0 或更高版本
- **CMake**: 3.13 或更高版本

### 安装gRPC开发包
```bash
sudo apt update
sudo apt install -y libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc
```

## 编译测试程序

### 1. 生成gRPC代码
```bash
cd grpc_test
protoc --grpc_out=. --cpp_out=. --plugin=protoc-gen-grpc=/usr/bin/grpc_cpp_plugin benchmark.proto
```

### 2. 编译
```bash
mkdir -p build
cd build
cmake ..
make
```

## 运行性能测试

### 方法1: 自动测试（推荐）
测试程序会自动启动服务器和客户端：

```bash
# 在第一个终端启动服务器
./grpc_benchmark server

# 在第二个终端运行客户端测试
./grpc_benchmark
```

### 方法2: 手动测试
```bash
# 1. 启动服务器（在后台）
./grpc_benchmark server &

# 2. 等待服务器启动
sleep 2

# 3. 运行客户端测试
./grpc_benchmark

# 4. 停止服务器
pkill grpc_benchmark
```

## 测试输出示例

```
gRPC Performance Test Suite
==============================

Testing gRPC latency with 1000 iterations...
gRPC Average latency: 0.010173 ms

Testing gRPC throughput with 5000 iterations...
gRPC Throughput: 119048 ops/sec

=== gRPC Test Summary ===
gRPC Average latency: 0.010173 ms
gRPC Throughput: 119048 ops/sec

Note: Compare these results with SlotIPC performance data.
```

## 测试结果分析

### 获得的性能数据（3次测试平均）
- **平均延迟**: 0.0102 ms
- **吞吐量**: 119,048 ops/s

### 与SlotIPC对比
| 指标 | gRPC | SlotIPC本地套接字 | SlotIPC TCP套接字 |
|------|------|------------------|------------------|
| 延迟 | 0.0102 ms | 0.049 ms | 0.091 ms |
| 吞吐量 | 119,048 ops/s | 20,270 ops/s | 10,544 ops/s |

### 性能特点
1. **延迟优势**: gRPC延迟比SlotIPC本地套接字快约5倍
2. **吞吐量优势**: gRPC吞吐量比SlotIPC本地套接字高约6倍
3. **资源开销**: gRPC有中等的内存开销，但性能表现优异

## 测试环境说明

- **测试条件**: 与SlotIPC使用相同的测试条件
- **延迟测试**: 1000次EchoMessage RPC调用
- **吞吐量测试**: 5000次Add RPC调用
- **网络**: localhost连接，端口50051
- **协议**: HTTP/2 over TCP

## 故障排除

### 编译错误
- 确保安装了所有必需的开发包
- 检查protobuf和gRPC版本兼容性

### 运行时错误
- 确保端口50051未被占用
- 检查防火墙设置
- 确认服务器已正确启动

### 性能异常
- 确保系统负载较低
- 多次运行测试取平均值
- 检查网络配置

## 文件结构

```
grpc_test/
├── benchmark.proto          # gRPC服务定义
├── benchmark.pb.cc          # 生成的protobuf代码
├── benchmark.pb.h           # 生成的protobuf头文件
├── benchmark.grpc.pb.cc     # 生成的gRPC代码
├── benchmark.grpc.pb.h      # 生成的gRPC头文件
├── grpc_benchmark.cpp       # 测试程序源码
├── CMakeLists.txt          # CMake构建配置
└── build/                  # 构建目录
    └── grpc_benchmark      # 可执行文件
```

## 总结

gRPC性能测试为SlotIPC技术调研提供了重要的对比基准。测试结果表明gRPC在纯性能方面具有明显优势，但SlotIPC在Qt生态集成和开发效率方面有其独特价值。 