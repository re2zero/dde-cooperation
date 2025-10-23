# SlotIPC 性能测试程序使用说明

## 概述

本文档介绍如何编译和运行 SlotIPC 性能测试程序，以获取技术调研报告中所需的实际性能数据。

## 环境要求

- **操作系统**：UOS 20/25, Deepin V23/V25
- **编译器**：GCC 12.3.0 或更高版本
- **Qt版本**：Qt 5.15.2 或 Qt 6.0.0
- **CMake**：3.13 或更高版本

## 编译步骤

### 1. 准备源码

确保您已经有完整的 dde-cooperation 项目源码，包含 SlotIPC 模块：

```bash
cd /path/to/dde-cooperation
ls src/infrastructure/slotipc  # 确认 SlotIPC 源码存在
```

### 2. 复制测试文件

将测试程序文件复制到项目根目录：

```bash
# 复制性能测试程序
cp slotipc_performance_test.cpp ./
cp CMakeLists_test.txt ./CMakeLists.txt
```

### 3. 编译测试程序

```bash
# 创建构建目录
mkdir build && cd build

# 配置CMake
cmake -DCMAKE_BUILD_TYPE=Release \
      -DSLOTIPC_BUILD_TESTS=ON \
      -DQT_DESIRED_VERSION=5 \
      ..

# 编译
make -j$(nproc)
```

### 4. 验证编译结果

```bash
# 检查可执行文件
ls -la slotipc_performance_test
file slotipc_performance_test
```

## 运行测试

### 基本性能测试

```bash
# 运行完整的性能测试套件
./slotipc_performance_test

# 输出示例：
# SlotIPC Performance Test Suite
# ==============================
# === Local Socket Performance Test ===
# Testing Local Socket latency with 1000 iterations...
# Local Socket Average latency: 0.156 ms
# Testing Local Socket throughput with 5000 iterations...
# Local Socket Throughput: 14823 ops/sec
# 
# === TCP Socket Performance Test ===
# Testing TCP Socket latency with 1000 iterations...
# TCP Socket Average latency: 0.284 ms
# Testing TCP Socket throughput with 5000 iterations...
# TCP Socket Throughput: 8156 ops/sec
# 
# === Memory Usage Test ===
# Initial memory: 12456 KB
# After init memory: 14678 KB
# Final memory: 14892 KB
# SlotIPC overhead: 2222 KB
```

### 重复测试获取平均值

为了获得更准确的数据，建议运行多次测试：

```bash
# 运行3次测试并保存结果
for i in {1..3}; do
    echo "=== Test Run $i ===" >> test_results.txt
    ./slotipc_performance_test >> test_results.txt 2>&1
    echo "" >> test_results.txt
done

# 查看结果
cat test_results.txt
```

### 系统资源监控

在测试期间监控系统资源使用：

```bash
# 在另一个终端中运行资源监控
top -p $(pgrep slotipc_performance_test)

# 或者使用 htop (如果可用)
htop -p $(pgrep slotipc_performance_test)
```

## 测试结果分析

### 延迟测试结果

- **本地套接字延迟**：通常在 0.1-0.2ms 范围内
- **TCP套接字延迟**：通常在 0.2-0.4ms 范围内
- **对比基准**：本地函数调用约 0.001ms

### 吞吐量测试结果

- **本地套接字吞吐量**：通常在 10K-20K ops/s 范围内
- **TCP套接字吞吐量**：通常在 5K-15K ops/s 范围内

### 内存使用分析

- **基础开销**：SlotIPC 库初始化约需 2-3MB 内存
- **运行时开销**：每个连接约增加 100-200KB 内存

## 故障排除

### 编译问题

**问题**：找不到 Qt 头文件
```bash
# 解决方案：安装 Qt 开发包
sudo apt install qtbase5-dev qtbase5-dev-tools  # Qt5
# 或
sudo apt install qt6-base-dev qt6-base-dev-tools  # Qt6
```

**问题**：CMake 配置失败
```bash
# 解决方案：检查 CMake 版本和依赖
cmake --version
sudo apt install cmake build-essential
```

### 运行时问题

**问题**：权限不足创建本地套接字
```bash
# 解决方案：确保有写入 /tmp 的权限
ls -la /tmp
chmod 755 /tmp
```

**问题**：TCP 端口被占用
```bash
# 解决方案：检查端口使用情况
netstat -tulpn | grep 12345
# 修改测试程序中的端口号或终止占用进程
```

## 数据收集

### 更新技术调研报告

将测试结果填入技术调研报告的相应位置：

1. 打开 `SlotIPC技术调研报告.md`
2. 找到 "5.4.2 性能测试结果" 部分
3. 将 "实测获得" 替换为实际测试数据
4. 更新图表中的数值

### 示例数据格式

```markdown
| 性能指标 | SlotIPC(Local) | SlotIPC(TCP) | 备注 |
|----------|----------------|--------------|------|
| 平均延迟 | 0.156ms | 0.284ms | 基于1000次调用 |
| 吞吐量 | 14,823 ops/s | 8,156 ops/s | 基于5000次异步调用 |
| 内存占用 | 2.2MB | 2.4MB | 包含库初始化开销 |
```

## 扩展测试

### 自定义测试参数

可以修改 `slotipc_performance_test.cpp` 中的参数：

```cpp
// 修改延迟测试的迭代次数
const int iterations = 1000;  // 改为 5000 进行更精确测试

// 修改吞吐量测试的迭代次数  
const int iterations = 5000;  // 改为 10000 进行压力测试
```

### 添加新的测试场景

可以在测试程序中添加：
- 大数据传输测试
- 并发连接测试
- 长时间稳定性测试
- 不同数据类型序列化性能测试

## 注意事项

1. **测试环境**：确保在相对空闲的系统上运行测试，避免其他程序干扰
2. **多次测试**：建议运行至少3次测试取平均值
3. **系统负载**：测试期间避免运行其他高负载程序
4. **网络环境**：TCP测试时确保网络环境稳定
5. **权限要求**：某些测试可能需要管理员权限

通过以上步骤，您可以获得真实可靠的 SlotIPC 性能数据，为技术调研报告提供有力的数据支撑。 