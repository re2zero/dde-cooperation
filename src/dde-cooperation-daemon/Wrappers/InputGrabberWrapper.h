#ifndef WRAPPERS_INPUTGRABBERWRAPPER_H
#define WRAPPERS_INPUTGRABBERWRAPPER_H

#include <filesystem>

#include <QObject>
#include <QProcess>

class QLocalServer;
class QLocalSocket;
class QProcess;

class Manager;
class Machine;

class InputGrabberWrapper : public QObject {
    Q_OBJECT

public:
    explicit InputGrabberWrapper(Manager *manager, const std::filesystem::path &path);
    ~InputGrabberWrapper();
    void setMachine(const std::weak_ptr<Machine> &machine);
    void start();
    void stop();

private slots:
    void onProcessClosed(int exitCode, QProcess::ExitStatus exitStatus);
    void handleNewConnection();
    void onReceived();
    void onDisconnected();

private:
    Manager *m_manager;
    QLocalServer *m_server;
    QLocalSocket *m_conn;
    QProcess *m_process;

    const QString m_path;

    uint8_t m_type;
    std::weak_ptr<Machine> m_machine;
};

#endif // !WRAPPERS_INPUTGRABBERWRAPPER_H
