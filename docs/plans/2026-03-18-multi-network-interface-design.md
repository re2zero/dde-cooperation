# Multi-Network Interface Support Design

## Overview

Implement multi-network interface support for dde-cooperation. When multiple valid network interfaces exist, users can select which network to use from the bottom IP status bar. Switching IP will synchronize discovery services and background daemon.

## Requirements Summary

| Decision | Choice |
|----------|--------|
| Switch Trigger | User confirmation dialog |
| Display Format | IP address only |
| Service Sync | Restart services |
| Default Selection | Remember last selection |

## Architecture

### Data Flow

```
Frontend UI (BottomLabel)
    ↓ selectedIpChanged(ip)
CooperationUtil
    ↓ IPC: IP_CHANGED
Background Daemon
    ↓ restart Announcer/Discoverer
```

### Components

1. **CommonUitls** - Network interface enumeration and selection management
2. **BottomLabel** - UI for IP selection with ComboBox
3. **CooperationUtil** - Frontend-side IP management and IPC bridge
4. **SendIpcService** - IPC notification to daemon
5. **HandleIpcService** - IPC message handling in daemon
6. **ServiceManager** - Discovery service lifecycle management

## Detailed Design

### Part 1: Core Data Model

#### NetworkInterfaceInfo Structure

Location: `src/common/commonutils.h`

```cpp
struct NetworkInterfaceInfo {
    QString ip;              // IP address
    QString interfaceName;   // Interface name (e.g., enp3s0, wlp2s0)
    int type;                // 0=Ethernet, 1=WiFi
};
```

#### CommonUitls New Functions

```cpp
// Get all valid network interface IPs (filter virtual, loopback, etc.)
static std::vector<NetworkInterfaceInfo> getAllAvailableIps();

// Get currently selected IP (from config, or first if none)
static std::string getSelectedIp();

// Set selected IP (persist to config)
static void setSelectedIp(const std::string& ip);
```

#### Config Persistence

- Key: `SelectedIPAddress`
- Default: Empty (auto-select first)
- Storage: ConfigManager/AppSettings

### Part 2: Frontend UI

#### BottomLabel Changes

Location: `src/lib/cooperation/core/gui/widgets/cooperationstatewidget.h/cpp`

**New Members**:
```cpp
class BottomLabel : public QWidget {
private:
    QComboBox *ipComboBox { nullptr };     // Replace ipLabel
    QString currentSelectedIp;             // Currently selected IP
    
    void updateIpList();                   // Refresh IP dropdown
    void onIpChanged(int index);           // Handle IP switch
    void showSwitchConfirmDialog();        // Confirmation dialog

signals:
    void ipChanged(const QString &ip);
};
```

**Switch Confirmation Dialog**:
- Title: "Switch Network"
- Message: "Switching network will disconnect existing connections. Continue?"
- Buttons: "Cancel", "Confirm"

### Part 3: Backend Service

#### IPC Message

- Type: `IP_CHANGED` (new message ID)
- Body: JSON with new IP address

#### SendIpcService Extension

```cpp
void SendIpcService::notifyIpChanged(const QString &newIp);
```

#### HandleIpcService Extension

Handle `IP_CHANGED` message and trigger service restart.

#### ServiceManager Changes

```cpp
void ServiceManager::restartDiscoveryServices(const QString &newIp);
```

**Flow**:
1. Stop Announcer and Discoverer
2. Update `_selectedIp`
3. Regenerate peer info with `genPeerInfo()`
4. Restart discovery services

### Part 4: Error Handling

| Scenario | Handling |
|----------|----------|
| User cancels switch | Revert ComboBox to original selection |
| Service restart fails | Show error dialog, revert to original IP |
| IPC timeout | Frontend waits 3s, shows error on failure |
| Selected IP becomes invalid | Auto-switch to first available IP |

### Part 5: Network Change Listener Enhancement

Modify `CooperationUtil::checkNetworkState()`:

1. Detect network interface changes
2. Check if selected IP is still valid
3. Auto-switch if selected IP is lost
4. Notify frontend to refresh IP list

## File Changes Summary

| File | Changes |
|------|---------|
| `src/common/commonutils.h/cpp` | Add `NetworkInterfaceInfo`, `getAllAvailableIps()`, `getSelectedIp()`, `setSelectedIp()` |
| `src/compat/common/commonutils.cpp` | Sync compat layer implementation |
| `src/lib/cooperation/core/gui/widgets/cooperationstatewidget.h/cpp` | Replace `ipLabel` with `ipComboBox`, add switch dialog |
| `src/lib/cooperation/core/utils/cooperationutil.h/cpp` | Add `getAllAvailableIps()`, `selectedIp()`, `setSelectedIp()`, enhance network monitoring |
| `src/compat/plugins/daemon/core/service/ipc/sendipcservice.h/cpp` | Add `notifyIpChanged()` |
| `src/compat/plugins/daemon/core/service/ipc/handleipcservice.cpp` | Handle `IP_CHANGED` message |
| `src/compat/plugins/daemon/core/service/servicemanager.h/cpp` | Add `restartDiscoveryServices()`, modify `genPeerInfo()` |
| `src/lib/cooperation/core/global_defines.h` | Add `SelectedIPAddressKey` |

## Test Scenarios

1. **Single Interface**: Behavior unchanged from current
2. **Multiple Interfaces**: Dropdown shows all valid IPs
3. **IP Switch**: Confirmation dialog, service restarts correctly
4. **Cancel Switch**: No changes made
5. **Network Change**: Auto-switch when selected IP is lost
6. **Persistence**: Selected IP persists across app restarts
7. **Error Recovery**: Graceful handling of service restart failures

## Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Service restart latency | Show loading indicator during restart |
| IPC message loss | Add acknowledgment mechanism |
| Race condition on rapid switches | Debounce switch requests (500ms) |
| Config file corruption | Validate config before applying |

## Timeline Estimate

- Phase 1: Core utilities (CommonUitls, config) - 2-3 hours
- Phase 2: Frontend UI (BottomLabel, dialog) - 2-3 hours  
- Phase 3: Backend IPC and services - 3-4 hours
- Phase 4: Integration and testing - 2-3 hours
- Total: ~10-13 hours
