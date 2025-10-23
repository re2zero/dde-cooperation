// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QCoreApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <QDebug>
#include <QThread>
#include <QProcess>
#include <iostream>

#include "slotipc/service.h"
#include "slotipc/interface.h"

class TestService : public QObject
{
    Q_OBJECT

public slots:
    QString echoMessage(const QString& message) {
        return "Echo: " + message;
    }
    
    int add(int a, int b) {
        return a + b;
    }
    
    void voidMethod() {
        // Do nothing, for testing void methods
    }

signals:
    void testSignal(const QString& data);
};

class PerformanceTest : public QObject
{
    Q_OBJECT

public:
    PerformanceTest(QObject* parent = nullptr) : QObject(parent) {}

    void runLocalSocketTest() {
        qDebug() << "=== Local Socket Performance Test ===";
        
        // Start server
        SlotIPCService service;
        TestService testObj;
        
        if (!service.listen("test_server", &testObj)) {
            qDebug() << "Failed to start local server";
            return;
        }
        
        // Connect client
        SlotIPCInterface interface;
        if (!interface.connectToServer("test_server")) {
            qDebug() << "Failed to connect to local server";
            return;
        }
        
        // Test latency
        testLatency(&interface, "Local Socket");
        
        // Test throughput
        testThroughput(&interface, "Local Socket");
        
        service.close();
    }
    
    void runTcpSocketTest() {
        qDebug() << "=== TCP Socket Performance Test ===";
        
        // Start server
        SlotIPCService service;
        TestService testObj;
        
        if (!service.listenTcp(QHostAddress::LocalHost, 12345, &testObj)) {
            qDebug() << "Failed to start TCP server";
            return;
        }
        
        // Connect client
        SlotIPCInterface interface;
        if (!interface.connectToServer(QHostAddress::LocalHost, 12345)) {
            qDebug() << "Failed to connect to TCP server";
            return;
        }
        
        // Test latency
        testLatency(&interface, "TCP Socket");
        
        // Test throughput
        testThroughput(&interface, "TCP Socket");
        
        service.close();
    }

private:
    void testLatency(SlotIPCInterface* interface, const QString& testType) {
        const int iterations = 1000;
        QElapsedTimer timer;
        
        qDebug() << QString("Testing %1 latency with %2 iterations...").arg(testType).arg(iterations);
        
        timer.start();
        for (int i = 0; i < iterations; ++i) {
            QString result;
            interface->call("echoMessage", Q_RETURN_ARG(QString, result), Q_ARG(QString, "test"));
        }
        qint64 elapsed = timer.elapsed();
        
        double avgLatency = static_cast<double>(elapsed) / iterations;
        qDebug() << QString("%1 Average latency: %2 ms").arg(testType).arg(avgLatency, 0, 'f', 3);
    }
    
    void testThroughput(SlotIPCInterface* interface, const QString& testType) {
        const int iterations = 5000;
        QElapsedTimer timer;
        
        qDebug() << QString("Testing %1 throughput with %2 iterations...").arg(testType).arg(iterations);
        
        timer.start();
        for (int i = 0; i < iterations; ++i) {
            interface->callNoReply("voidMethod");
        }
        
        // Wait a bit for async calls to complete
        QThread::msleep(100);
        
        qint64 elapsed = timer.elapsed();
        double throughput = static_cast<double>(iterations) / (elapsed / 1000.0);
        
        qDebug() << QString("%1 Throughput: %2 ops/sec").arg(testType).arg(throughput, 0, 'f', 0);
    }
};

class MemoryTest : public QObject
{
    Q_OBJECT
    
public:
    void testMemoryUsage() {
        qDebug() << "=== Memory Usage Test ===";
        
        // Get initial memory
        size_t initialMemory = getCurrentMemoryUsage();
        
        // Create service and interface
        SlotIPCService service;
        TestService testObj;
        service.listen("memory_test", &testObj);
        
        SlotIPCInterface interface;
        interface.connectToServer("memory_test");
        
        // Get memory after initialization
        size_t afterInitMemory = getCurrentMemoryUsage();
        
        // Perform some operations
        for (int i = 0; i < 1000; ++i) {
            QString result;
            interface.call("echoMessage", Q_RETURN_ARG(QString, result), Q_ARG(QString, "memory test"));
        }
        
        // Get final memory
        size_t finalMemory = getCurrentMemoryUsage();
        
        qDebug() << QString("Initial memory: %1 KB").arg(initialMemory / 1024);
        qDebug() << QString("After init memory: %1 KB").arg(afterInitMemory / 1024);
        qDebug() << QString("Final memory: %1 KB").arg(finalMemory / 1024);
        qDebug() << QString("SlotIPC overhead: %1 KB").arg((afterInitMemory - initialMemory) / 1024);
        
        service.close();
    }
    
private:
    size_t getCurrentMemoryUsage() {
        // Simple memory usage estimation for Linux
        QProcess proc;
        proc.start("ps", QStringList() << "-o" << "rss=" << "-p" << QString::number(QCoreApplication::applicationPid()));
        proc.waitForFinished();
        QString output = proc.readAllStandardOutput();
        return output.trimmed().toULongLong() * 1024; // Convert KB to bytes
    }
};

class BenchmarkTest : public QObject
{
    Q_OBJECT
    
public:
    void testLocalFunctionCall() {
        qDebug() << "=== Local Function Call Benchmark ===";
        
        const int iterations = 1000000;
        QElapsedTimer timer;
        
        qDebug() << QString("Testing local function call with %1 iterations...").arg(iterations);
        
        timer.start();
        for (int i = 0; i < iterations; ++i) {
            QString result = echoLocal("test");  // 本地函数调用
            Q_UNUSED(result)
        }
        qint64 elapsed = timer.elapsed();
        
        double avgLatency = static_cast<double>(elapsed) / iterations;
        qDebug() << QString("Local function call average latency: %1 ms").arg(avgLatency, 0, 'f', 6);
    }
    
private:
    QString echoLocal(const QString& message) {
        return "Echo: " + message;
    }
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    qDebug() << "SlotIPC Performance Test Suite";
    qDebug() << "==============================";
    qDebug() << "";
    
    // 基准测试
    BenchmarkTest benchmark;
    benchmark.testLocalFunctionCall();
    qDebug() << "";
    
    // SlotIPC测试
    PerformanceTest perfTest;
    perfTest.runLocalSocketTest();
    qDebug() << "";
    perfTest.runTcpSocketTest();
    qDebug() << "";
    
    // 内存测试
    MemoryTest memTest;
    memTest.testMemoryUsage();
    
    qDebug() << "";
    qDebug() << "=== Test Summary ===";
    qDebug() << "All tests completed. Please compare the results:";
    qDebug() << "1. Local function call: ~0.000x ms (baseline)";
    qDebug() << "2. SlotIPC Local Socket: ~x.xxx ms";
    qDebug() << "3. SlotIPC TCP Socket: ~x.xxx ms";
    qDebug() << "";
    qDebug() << "Note: For complete comparison, implement gRPC benchmark separately.";
    
    return 0;
}

#include "slotipc_performance_test.moc" 