# Multi-Network Interface Support Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Enable users to select which network interface to use when multiple valid networks exist, with automatic service synchronization on switch.

**Architecture:** Frontend-driven approach where BottomLabel UI triggers IP selection changes, persisted via ConfigManager, and propagated to background daemon via IPC to restart discovery services.

**Tech Stack:** Qt5/Qt6, DTK Widgets, co/cout IPC, UDP multicast discovery

---

## Task 1: Add NetworkInterfaceInfo Structure and Core Functions

**Files:**
- Modify: `src/common/commonutils.h`
- Modify: `src/common/commonutils.cpp`
- Modify: `src/compat/common/commonutils.h`
- Modify: `src/compat/common/commonutils.cpp`

**Step 1: Add NetworkInterfaceInfo structure to commonutils.h**

In `src/common/commonutils.h`, add before `CommonUitls` class:

```cpp
#include <vector>

struct NetworkInterfaceInfo {
    std::string ip;           // IP address
    std::string interfaceName; // Interface name (e.g., enp3s0)
    int type;                  // 0=Ethernet, 1=WiFi
};
```

**Step 2: Add new function declarations to CommonUitls class**

In `src/common/commonutils.h`, add to `CommonUitls` class public section:

```cpp
static std::vector<NetworkInterfaceInfo> getAllAvailableIps();
static std::string getSelectedIp();
static void setSelectedIp(const std::string& ip);
```

**Step 3: Implement getAllAvailableIps() in commonutils.cpp**

In `src/common/commonutils.cpp`, add after `getFirstIp()`:

```cpp
std::vector<NetworkInterfaceInfo> CommonUitls::getAllAvailableIps()
{
    std::vector<NetworkInterfaceInfo> result;
    
    foreach (QNetworkInterface netInterface, QNetworkInterface::allInterfaces()) {
        if (!netInterface.flags().testFlag(QNetworkInterface::IsRunning)
            || (netInterface.type() != QNetworkInterface::Ethernet
                && netInterface.type() != QNetworkInterface::Wifi)) {
            continue;
        }

        if (netInterface.name().startsWith("virbr") || netInterface.name().startsWith("vmnet")
            || netInterface.name().startsWith("docker")) {
            continue;
        }

        QList<QNetworkAddressEntry> entryList = netInterface.addressEntries();
        foreach (QNetworkAddressEntry entry, entryList) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol 
                && entry.ip() != QHostAddress::LocalHost) {
                NetworkInterfaceInfo info;
                info.ip = entry.ip().toString().toStdString();
                info.interfaceName = netInterface.name().toStdString();
                info.type = (netInterface.type() == QNetworkInterface::Ethernet) ? 0 : 1;
                result.push_back(info);
            }
        }
    }
    
    qInfo() << "Found" << result.size() << "available network interfaces";
    return result;
}
```

**Step 4: Implement getSelectedIp() and setSelectedIp()**

In `src/common/commonutils.cpp`, add:

```cpp
#include <QSettings>

static const char* kSelectedIpKey = "SelectedIPAddress";

std::string CommonUitls::getSelectedIp()
{
    QString configPath = QString("%1/%2/%3/config.conf")
        .arg(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation))
        .arg(qApp->organizationName())
        .arg(qApp->applicationName());
    
    QSettings settings(configPath, QSettings::IniFormat);
    QString selectedIp = settings.value(kSelectedIpKey).toString();
    
    if (selectedIp.isEmpty()) {
        // No saved selection, return first available
        return getFirstIp();
    }
    
    // Verify the saved IP is still valid
    auto allIps = getAllAvailableIps();
    for (const auto& info : allIps) {
        if (info.ip == selectedIp.toStdString()) {
            return selectedIp.toStdString();
        }
    }
    
    // Saved IP no longer valid, return first available
    return getFirstIp();
}

void CommonUitls::setSelectedIp(const std::string& ip)
{
    QString configPath = QString("%1/%2/%3/config.conf")
        .arg(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation))
        .arg(qApp->organizationName())
        .arg(qApp->applicationName());
    
    QSettings settings(configPath, QSettings::IniFormat);
    settings.setValue(kSelectedIpKey, QString::fromStdString(ip));
    settings.sync();
    
    qInfo() << "Saved selected IP:" << ip.c_str();
}
```

**Step 5: Sync compat layer**

In `src/compat/common/commonutils.h`, add same structure and declarations.
In `src/compat/common/commonutils.cpp`, add same implementations.

**Step 6: Build and verify compilation**

Run: `cmake --build build --target dde-cooperation 2>&1 | head -50`
Expected: No compilation errors related to new functions

**Step 7: Commit**

```bash
git add src/common/commonutils.h src/common/commonutils.cpp
git add src/compat/common/commonutils.h src/compat/common/commonutils.cpp
git commit -m "feat(network): add getAllAvailableIps and IP selection functions"
```

---

## Task 2: Add IPC Message Type for IP Change

**Files:**
- Modify: `src/common/constant.h`

**Step 1: Add IP_CHANGED message constant**

In `src/common/constant.h`, find existing message type definitions and add:

```cpp
// IPC message types
#define IP_CHANGED  1001
```

**Step 2: Commit**

```bash
git add src/common/constant.h
git commit -m "feat(ipc): add IP_CHANGED message constant"
```

---

## Task 3: Extend SendIpcService with IP Change Notification

**Files:**
- Modify: `src/compat/plugins/daemon/core/service/ipc/sendipcservice.h`
- Modify: `src/compat/plugins/daemon/core/service/ipc/sendipcservice.cpp`

**Step 1: Add notifyIpChanged declaration**

In `sendipcservice.h`, add to public section:

```cpp
void notifyIpChanged(const QString &newIp);
```

**Step 2: Implement notifyIpChanged**

In `sendipcservice.cpp`, add:

```cpp
#include "common/constant.h"

void SendIpcService::notifyIpChanged(const QString &newIp)
{
    DLOG << "Notifying daemon of IP change to:" << newIp.toStdString();
    co::Json json;
    json.add_member("ip", newIp.toStdString());
    handleSendToClient("dde-cooperation", IP_CHANGED, json.str().c_str());
}
```

**Step 3: Commit**

```bash
git add src/compat/plugins/daemon/core/service/ipc/sendipcservice.h
git add src/compat/plugins/daemon/core/service/ipc/sendipcservice.cpp
git commit -m "feat(ipc): add notifyIpChanged to SendIpcService"
```

---

## Task 4: Handle IP_CHANGED in HandleIpcService

**Files:**
- Modify: `src/compat/plugins/daemon/core/service/ipc/handleipcservice.cpp`

**Step 1: Add IP_CHANGED case to message handler**

Find the message handling switch/if block and add case for `IP_CHANGED`:

```cpp
#include "common/constant.h"
#include "servicemanager.h"

// In message handling function:
case IP_CHANGED: {
    co::Json json;
    if (json.parse_from(msg.toStdString())) {
        fastring ip = json.get("ip").as_string();
        DLOG << "Received IP_CHANGED, new IP:" << ip;
        ServiceManager::instance()->restartDiscoveryServices(QString(ip.c_str()));
    }
    break;
}
```

**Step 2: Commit**

```bash
git add src/compat/plugins/daemon/core/service/ipc/handleipcservice.cpp
git commit -m "feat(ipc): handle IP_CHANGED message in HandleIpcService"
```

---

## Task 5: Add restartDiscoveryServices to ServiceManager

**Files:**
- Modify: `src/compat/plugins/daemon/core/service/servicemanager.h`
- Modify: `src/compat/plugins/daemon/core/service/servicemanager.cpp`

**Step 1: Add member and method declaration**

In `servicemanager.h`, add to private section:

```cpp
std::string _selectedIp;

public:
    void restartDiscoveryServices(const QString &newIp);
```

**Step 2: Implement restartDiscoveryServices**

In `servicemanager.cpp`, add:

```cpp
void ServiceManager::restartDiscoveryServices(const QString &newIp)
{
    DLOG << "Restarting discovery services with IP:" << newIp.toStdString();
    
    // Stop existing services
    DiscoveryJob::instance()->stopAnnouncer();
    DiscoveryJob::instance()->stopDiscoverer();
    
    // Update selected IP
    _selectedIp = newIp.toStdString();
    
    // Regenerate peer info and restart services
    fastring baseinfo = genPeerInfo();
    
    QUNIGO([this, baseinfo]() {
        DiscoveryJob::instance()->discovererRun();
        DiscoveryJob::instance()->announcerRun(baseinfo);
    });
    
    DLOG << "Discovery services restarted successfully";
}
```

**Step 3: Modify genPeerInfo to use selected IP**

In `servicemanager.cpp`, modify `genPeerInfo()`:

Find the line:
```cpp
{ "ipv4", Util::getFirstIp() },
```

Replace with:
```cpp
{ "ipv4", _selectedIp.empty() ? Util::getFirstIp() : _selectedIp },
```

**Step 4: Commit**

```bash
git add src/compat/plugins/daemon/core/service/servicemanager.h
git add src/compat/plugins/daemon/core/service/servicemanager.cpp
git commit -m "feat(daemon): add restartDiscoveryServices and use selected IP"
```

---

## Task 6: Add SelectedIPAddressKey to Global Defines

**Files:**
- Modify: `src/lib/cooperation/core/global_defines.h`

**Step 1: Add new key constant**

In `global_defines.h`, add to `AppSettings` namespace:

```cpp
inline constexpr char SelectedIPAddressKey[] { "SelectedIPAddress" };
```

**Step 2: Commit**

```bash
git add src/lib/cooperation/core/global_defines.h
git commit -m "feat(config): add SelectedIPAddressKey constant"
```

---

## Task 7: Extend CooperationUtil with IP Management

**Files:**
- Modify: `src/lib/cooperation/core/utils/cooperationutil.h`
- Modify: `src/lib/cooperation/core/utils/cooperationutil.cpp`

**Step 1: Add method declarations**

In `cooperationutil.h`, add to public section:

```cpp
static QList<QPair<QString, QString>> getAllAvailableIps(); // returns <ip, interfaceName>
static QString selectedIp();
static void setSelectedIp(const QString &ip);

signals:
    void ipListChanged();
    void selectedIpChanged(const QString &ip);
```

**Step 2: Implement getAllAvailableIps**

In `cooperationutil.cpp`, add:

```cpp
#include <common/commonutils.h>

QList<QPair<QString, QString>> CooperationUtil::getAllAvailableIps()
{
    QList<QPair<QString, QString>> result;
    auto ips = deepin_cross::CommonUitls::getAllAvailableIps();
    for (const auto& info : ips) {
        result.append(qMakePair(
            QString::fromStdString(info.ip),
            QString::fromStdString(info.interfaceName)
        ));
    }
    return result;
}
```

**Step 3: Implement selectedIp and setSelectedIp**

```cpp
QString CooperationUtil::selectedIp()
{
    return QString::fromStdString(deepin_cross::CommonUitls::getSelectedIp());
}

void CooperationUtil::setSelectedIp(const QString &ip)
{
    deepin_cross::CommonUitls::setSelectedIp(ip.toStdString());
}
```

**Step 4: Commit**

```bash
git add src/lib/cooperation/core/utils/cooperationutil.h
git add src/lib/cooperation/core/utils/cooperationutil.cpp
git commit -m "feat(util): add IP management functions to CooperationUtil"
```

---

## Task 8: Replace ipLabel with ipComboBox in BottomLabel

**Files:**
- Modify: `src/lib/cooperation/core/gui/widgets/cooperationstatewidget.h`
- Modify: `src/lib/cooperation/core/gui/widgets/cooperationstatewidget.cpp`

**Step 1: Update BottomLabel class declaration**

In `cooperationstatewidget.h`, modify `BottomLabel` class:

```cpp
class BottomLabel : public QWidget
{
    Q_OBJECT
public:
    explicit BottomLabel(QWidget *parent = nullptr);

    void setIp(const QString &ip);
    void showDialog() const;
    void onSwitchMode(int page);
    void updateIpList();  // NEW

protected:
    void paintEvent(QPaintEvent *) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void updateSizeMode();
    void onIpChanged(int index);  // NEW

signals:
    void ipChanged(const QString &ip);  // NEW

private:
    void initUI();
    void showSwitchConfirmDialog(const QString &newIp);  // NEW

private:
    CooperationAbstractDialog *dialog { nullptr };
    QStackedLayout *stackedLayout { nullptr };
    QLabel *tipLabel { nullptr };
    QComboBox *ipComboBox { nullptr };  // CHANGED from ipLabel
    QString currentSelectedIp;  // NEW
    QTimer *timer { nullptr };
};
```

**Step 2: Update initUI to use ComboBox**

In `cooperationstatewidget.cpp`, modify `BottomLabel::initUI()`:

Replace:
```cpp
QString ip = QString(tr("Local IP: %1").arg("---"));
ipLabel = new QLabel(ip);
ipLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
CooperationGuiHelper::setAutoFont(ipLabel, 12, QFont::Normal);
```

With:
```cpp
ipComboBox = new QComboBox(this);
ipComboBox->setMinimumWidth(150);
ipComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
connect(ipComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &BottomLabel::onIpChanged);

// Initialize with current IPs
updateIpList();
```

And in the layout section, replace `ipLabel` with `ipComboBox`:
```cpp
hLayout->addWidget(ipComboBox);
```

**Step 3: Implement updateIpList**

```cpp
void BottomLabel::updateIpList()
{
    DLOG << "Updating IP list";
    ipComboBox->blockSignals(true);
    ipComboBox->clear();
    
    auto ips = CooperationUtil::getAllAvailableIps();
    QString currentIp = CooperationUtil::selectedIp();
    
    for (const auto& pair : ips) {
        ipComboBox->addItem(pair.first);
    }
    
    // Set current selection
    int index = ipComboBox->findText(currentIp);
    if (index >= 0) {
        ipComboBox->setCurrentIndex(index);
        currentSelectedIp = currentIp;
    } else if (!ips.isEmpty()) {
        ipComboBox->setCurrentIndex(0);
        currentSelectedIp = ips.first().first;
    }
    
    ipComboBox->blockSignals(false);
    DLOG << "IP list updated, current:" << currentSelectedIp.toStdString();
}
```

**Step 4: Implement onIpChanged**

```cpp
void BottomLabel::onIpChanged(int index)
{
    if (index < 0) return;
    
    QString newIp = ipComboBox->itemText(index);
    DLOG << "IP selection changed to:" << newIp.toStdString();
    
    if (newIp == currentSelectedIp) {
        DLOG << "Same IP, ignoring";
        return;
    }
    
    showSwitchConfirmDialog(newIp);
}
```

**Step 5: Implement showSwitchConfirmDialog**

```cpp
void BottomLabel::showSwitchConfirmDialog(const QString &newIp)
{
    DLOG << "Showing switch confirmation dialog";
    
#ifdef __linux__
    CooperationDialog dlg(this);
    dlg.setIcon(QIcon::fromTheme("dialog-warning"));
    dlg.setTitle(tr("Switch Network"));
    dlg.setMessage(tr("Switching network will disconnect existing connections. Continue?"));
    dlg.addButton(tr("Cancel"));
    dlg.addButton(tr("Confirm"), true, DDialog::ButtonWarning);
    
    if (dlg.exec() == QDialog::Accepted) {
        DLOG << "User confirmed IP switch";
        currentSelectedIp = newIp;
        emit ipChanged(newIp);
    } else {
        DLOG << "User cancelled IP switch, reverting";
        ipComboBox->blockSignals(true);
        ipComboBox->setCurrentText(currentSelectedIp);
        ipComboBox->blockSignals(false);
    }
#else
    // Windows implementation
    QMessageBox dlg(this);
    dlg.setWindowTitle(tr("Switch Network"));
    dlg.setText(tr("Switching network will disconnect existing connections. Continue?"));
    dlg.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
    dlg.setDefaultButton(QMessageBox::Cancel);
    
    if (dlg.exec() == QMessageBox::Ok) {
        currentSelectedIp = newIp;
        emit ipChanged(newIp);
    } else {
        ipComboBox->blockSignals(true);
        ipComboBox->setCurrentText(currentSelectedIp);
        ipComboBox->blockSignals(false);
    }
#endif
}
```

**Step 6: Update setIp to use ComboBox**

```cpp
void BottomLabel::setIp(const QString &ip)
{
    DLOG << "setIp called with:" << ip.toStdString();
    int index = ipComboBox->findText(ip);
    if (index >= 0) {
        ipComboBox->blockSignals(true);
        ipComboBox->setCurrentIndex(index);
        ipComboBox->blockSignals(false);
        currentSelectedIp = ip;
    }
}
```

**Step 7: Add includes at top of file**

```cpp
#include <QComboBox>
#include <QMessageBox>
#include "utils/cooperationutil.h"
```

**Step 8: Commit**

```bash
git add src/lib/cooperation/core/gui/widgets/cooperationstatewidget.h
git add src/lib/cooperation/core/gui/widgets/cooperationstatewidget.cpp
git commit -m "feat(ui): replace ipLabel with ipComboBox for network selection"
```

---

## Task 9: Connect BottomLabel ipChanged Signal to Main Logic

**Files:**
- Modify: `src/lib/cooperation/core/gui/mainwindow.cpp`
- Modify: `src/lib/cooperation/core/gui/mainwindow_p.h`

**Step 1: Add IPC sender include**

In `mainwindow.cpp`, ensure include for SendIpcService is present (may need path adjustment):

```cpp
// Note: This may require adding the daemon IPC path to include directories
// or using a bridge/signal mechanism
```

**Step 2: Connect signal in MainWindowPrivate::initConnect**

In `mainwindow.cpp`, add to `initConnect()`:

```cpp
connect(d->bottomLabel, &BottomLabel::ipChanged, this, [this](const QString &newIp) {
    DLOG << "Main window received IP change:" << newIp.toStdString();
    
    // Persist selection
    CooperationUtil::setSelectedIp(newIp);
    
    // Notify daemon via IPC
    // Note: This requires IPC bridge or direct call
    // For now, we use the existing IPC mechanism
    SendIpcService::instance()->notifyIpChanged(newIp);
});
```

**Step 3: Add necessary include**

```cpp
#include "service/ipc/sendipcservice.h"
```

Note: This may require build system changes to include paths.

**Step 4: Commit**

```bash
git add src/lib/cooperation/core/gui/mainwindow.cpp
git commit -m "feat(ui): connect IP change signal to daemon notification"
```

---

## Task 10: Update Network State Monitoring

**Files:**
- Modify: `src/lib/cooperation/core/utils/cooperationutil.cpp`

**Step 1: Enhance checkNetworkState**

Modify `CooperationUtil::checkNetworkState()` to detect IP list changes:

```cpp
void CooperationUtil::checkNetworkState()
{
    static QList<QPair<QString, QString>> lastKnownIps;
    
    auto currentIps = getAllAvailableIps();
    bool isConnected = !currentIps.isEmpty();
    
    // Check for IP list changes
    if (currentIps != lastKnownIps) {
        DLOG << "IP list changed, old:" << lastKnownIps.size() 
             << "new:" << currentIps.size();
        lastKnownIps = currentIps;
        Q_EMIT ipListChanged();
    }
    
    // Check if selected IP is still valid
    QString selected = selectedIp();
    bool selectedStillValid = false;
    for (const auto& pair : currentIps) {
        if (pair.first == selected) {
            selectedStillValid = true;
            break;
        }
    }
    
    if (!selectedStillValid && !currentIps.isEmpty()) {
        DLOG << "Selected IP no longer valid, auto-switching to:" << currentIps.first().first;
        setSelectedIp(currentIps.first().first);
        Q_EMIT selectedIpChanged(currentIps.first().first);
    }
    
    if (isConnected != d->isOnline) {
        DLOG << "Network state changed from" << d->isOnline << "to" << isConnected;
        d->isOnline = isConnected;
        Q_EMIT onlineStateChanged(selectedIp());
    }
}
```

**Step 2: Commit**

```bash
git add src/lib/cooperation/core/utils/cooperationutil.cpp
git commit -m "feat(network): enhance network monitoring for IP list changes"
```

---

## Task 11: Integration Build and Test

**Step 1: Full build**

```bash
cmake --build build -j$(nproc) 2>&1 | tee build.log
```

Expected: Build succeeds without errors

**Step 2: Check for missing includes or link errors**

```bash
grep -i "error:" build.log || echo "No errors found"
```

**Step 3: Manual test checklist**

- [ ] App starts with single network - behavior unchanged
- [ ] App starts with multiple networks - ComboBox shows all IPs
- [ ] Selecting different IP shows confirmation dialog
- [ ] Cancel reverts selection
- [ ] Confirm triggers service restart
- [ ] Selected IP persists after app restart
- [ ] Disconnecting network auto-switches to remaining

---

## Task 12: Final Commit and Summary

**Step 1: Review all changes**

```bash
git log --oneline -15
git diff origin/bugfix..HEAD --stat
```

**Step 2: Create summary commit if needed**

```bash
git add -A
git commit -m "feat(network): complete multi-network interface support

- Add NetworkInterfaceInfo structure and getAllAvailableIps()
- Add IP selection persistence via config
- Add IP_CHANGED IPC message for daemon notification
- Replace static IP label with ComboBox selector
- Add confirmation dialog on network switch
- Auto-switch when selected IP becomes invalid
- Restart discovery services on IP change"
```

---

## File Changes Summary

| File | Type | Description |
|------|------|-------------|
| `src/common/commonutils.h` | Modify | Add NetworkInterfaceInfo, new functions |
| `src/common/commonutils.cpp` | Modify | Implement getAllAvailableIps, getSelectedIp, setSelectedIp |
| `src/compat/common/commonutils.*` | Modify | Sync compat layer |
| `src/common/constant.h` | Modify | Add IP_CHANGED constant |
| `src/compat/plugins/daemon/core/service/ipc/sendipcservice.*` | Modify | Add notifyIpChanged |
| `src/compat/plugins/daemon/core/service/ipc/handleipcservice.cpp` | Modify | Handle IP_CHANGED |
| `src/compat/plugins/daemon/core/service/servicemanager.*` | Modify | Add restartDiscoveryServices |
| `src/lib/cooperation/core/global_defines.h` | Modify | Add SelectedIPAddressKey |
| `src/lib/cooperation/core/utils/cooperationutil.*` | Modify | Add IP management |
| `src/lib/cooperation/core/gui/widgets/cooperationstatewidget.*` | Modify | Replace label with ComboBox |
| `src/lib/cooperation/core/gui/mainwindow.cpp` | Modify | Connect IP change signal |
