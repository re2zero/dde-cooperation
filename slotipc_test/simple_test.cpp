#include <QCoreApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <QDebug>
#include <QThread>
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
        // Do nothing
    }

signals:
    void testSignal(const QString& data);
};

class SimpleTest : public QObject
{
    Q_OBJECT

public:
    void runTest() {
        qDebug() << "Starting SlotIPC Performance Test...";
        qDebug() << "=====================================";
        
        testLocalSocket();
        testTcpSocket();
        
        qDebug() << "=====================================";
        qDebug() << "All tests completed.";
        
        QCoreApplication::quit();
    }

private:
    void testLocalSocket() {
        qDebug() << "\n=== Local Socket Test ===";
        
        SlotIPCService service;
        TestService testObj;
        
        if (!service.listen("test_server", &testObj)) {
            qDebug() << "Failed to start local server";
            return;
        }
        
        SlotIPCInterface interface;
        if (!interface.connectToServer("test_server")) {
            qDebug() << "Failed to connect to local server";
            return;
        }
        
        // 延迟测试
        const int iterations = 100; // 减少迭代次数避免问题
        QElapsedTimer timer;
        
        qDebug() << QString("Testing latency with %1 iterations...").arg(iterations);
        timer.start();
        
        for (int i = 0; i < iterations; ++i) {
            QString result;
            interface.call("echoMessage", Q_RETURN_ARG(QString, result), Q_ARG(QString, "test"));
        }
        
        qint64 elapsed = timer.elapsed();
        double avgLatency = static_cast<double>(elapsed) / iterations;
        
        qDebug() << QString("Local Socket - Average latency: %1 ms").arg(avgLatency, 0, 'f', 3);
        
        service.close();
    }
    
    void testTcpSocket() {
        qDebug() << "\n=== TCP Socket Test ===";
        
        SlotIPCService service;
        TestService testObj;
        
        if (!service.listenTcp(QHostAddress::LocalHost, 12345, &testObj)) {
            qDebug() << "Failed to start TCP server";
            return;
        }
        
        SlotIPCInterface interface;
        if (!interface.connectToServer(QHostAddress::LocalHost, 12345)) {
            qDebug() << "Failed to connect to TCP server";
            return;
        }
        
        // 延迟测试
        const int iterations = 100;
        QElapsedTimer timer;
        
        qDebug() << QString("Testing latency with %1 iterations...").arg(iterations);
        timer.start();
        
        for (int i = 0; i < iterations; ++i) {
            QString result;
            interface.call("echoMessage", Q_RETURN_ARG(QString, result), Q_ARG(QString, "test"));
        }
        
        qint64 elapsed = timer.elapsed();
        double avgLatency = static_cast<double>(elapsed) / iterations;
        
        qDebug() << QString("TCP Socket - Average latency: %1 ms").arg(avgLatency, 0, 'f', 3);
        
        service.close();
    }
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    SimpleTest test;
    QTimer::singleShot(100, &test, &SimpleTest::runTest);
    
    return app.exec();
}

#include "simple_test.moc"
