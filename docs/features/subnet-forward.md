# 子网转发（subnet-forward）

会话级受控子网转发：在单向网络（发起方 → 接收方 TCP 单向可达）中，协同会话
建立后，接收方访问"配置网段"的所有 TCP 流量经发起方的应用层转发到真实目标。
会话既是控制面也是数据面；会话断开整体失效、自动回直连。

复用既有反向隧道设施（TASK-396845）：`TunnelManager` 的 `subnet-forward`
服务（OPEN 帧携带 `dst:port`），协议对老对端兼容（未知服务名，拒绝不崩溃）。

```
B（被拦截侧，10.8.15.x）                     A（服务侧，10.8.12.90）
 ┌────────────────────────────┐              ┌──────────────────────────┐
 │ 应用 → nft OUTPUT redirect │   TCP 单向    │ TunnelManager 校验+拨号  │
 │  → 127.0.0.1:proxy         │ ───────────→ │  → 真实目标 dst:port     │
 │  → SO_ORIGINAL_DST         │   TLS 会话    │  （仅认证会话可申请）    │
 │  → openDynamicStream       │   51598       │                          │
 └────────────────────────────┘              └──────────────────────────┘
```

## 组件

| 组件 | 位置 | 职责 |
| --- | --- | --- |
| TunnelManager 动态目标 | `src/lib/common/manager/tunnelmanager.*` | OPEN 校验（网段/端口/限额）、拨号、转发、审计 |
| 共享解析 | `src/lib/common/manager/subnetforwardutil.h` | 严格 IPv4 CIDR 与 `ip:port` 解析（各端一致、fail-closed） |
| 规则生成 | `src/lib/common/manager/subnetforwardrules.h` | nft/iptables OUTPUT redirect 规则文本（专属命名，幂等装卸） |
| 透明代理 | `src/lib/common/manager/subnetforwardproxy.*` | 只绑 127.0.0.1 临时端口；SO_ORIGINAL_DST 取原目标；第二道门 |
| 控制器 | `src/lib/cooperation/core/net/subnetforward/subnetforwardcontroller.*` | 生命周期：激活/去激活/租约续期/回直连 |
| 配置 | `src/lib/cooperation/core/net/subnetforward/subnetforwardconfig.*` | 读配置组，产出两种策略（服务侧带跳板防护） |
| 特权助手 | `src/apps/forward-helper/` | system bus 服务（D-Bus 激活，root）；polkit 授权后执行 nft/iptables |

## 配置（cooperation_settings，组 `SubnetForward`，运行时生效，v1 无 UI）

| 键 | 默认 | 说明 |
| --- | --- | --- |
| `Enabled` | false | 功能总开关 |
| `UsePeerSubnet` | false | B 角色：允许本机出方向流量经对端转发 |
| `ServePeer` | false | A 角色：愿意为对端提供转发 |
| `Networks` | 空 | CIDR 列表；**空 = 功能不生效** |
| `AllowedHosts` | 空 | 网段内再限 IP；空 = 整网段 |
| `AllowedPorts` | 空 | 端口/范围（`22`、`80,443`、`60000-60010`）；空 = 除拒绝外全部 |
| `DeniedPorts` | 51598 | 拒绝端口表；**51598 强制注入**（防会话自环） |
| `DeviceWhitelist` | 空 | 对端 IPv4 列表；**语义已定死：空 = 拒绝所有对端**（fail-closed，列出至少一个地址即为运维显式决定） |
| `MaxStreams` | 16 | 并发流上限（对齐既有 service 上限） |
| `MaxBytesPerSecond` | 0 | 全局带宽上限（0 = 不限；作用于目标→对端方向，超支即停读，借 TCP 背压限速） |
| `IdleTimeoutMs` | 0 | 空闲流超时（0 = 不启用） |
| `DialTimeoutMs` | 0 | 拨号超时（0 = 默认 5s） |
| `AllowNonLocalNetworks` | false | **跳板防护**：默认服务策略只保留包含本机地址的网段；转发到其他网段属高危，须显式打开 |

示例（A 机为 10.8.12.90）：

```ini
[SubnetForward]
Enabled=true
ServePeer=true
Networks=10.8.12.0/24
DeniedPorts=51598
DeviceWhitelist=10.8.15.60
```

B 机：`Enabled=true`、`UsePeerSubnet=true`、`Networks=10.8.12.0/24`、
`DeviceWhitelist=10.8.12.90`。

## 协商与激活

1. 双向 share 握手（`APPLY_SHARE` / `APPLY_SHARE_RESULT`）新增
   `subnet_forward` 位，语义为"**我愿意为你提供转发**"。老对端无此字段，
   解析为 false；字段畸形不破坏解析。
2. 服务侧：协同确认时按本地配置安装服务策略（策略只拦截入站动态 OPEN，
   装早了/没人用都无副作用）。
3. 使用侧：`BARRIER_READY` 后，对端服务位 + 本机三层开关 + 网段非空 +
   设备白名单全部满足才激活；任一不满足则功能完全惰性，行为与现状一致。

### 已定案的实现取舍

- **直连探测劫持**（二选一取"激活期跳过直连回退"）：转发激活期间对端的
  直连探测/8 秒回退包会进隧道且永远"通"。`ShareHelper` 的 barrier 探测与
  BARRIER_READY 超时回退在 `isForwardingPeer(ip)` 时直接走隧道路径并附
  代码注释。（fwmark 方案要求应用持 CAP_NET_ADMIN，已否决。）
- **回流环路**：nft/iptables 规则先放行 loopback 与 `tcp dport 51598`
  （deny 表强制含会话口）再 redirect，隧道无法劫持会话自身。
- **TCP-only**：UDP/DNS 不覆盖、不拦截，行为与升级前一致。
- 规则只触碰专属命名资源（nft 表 `dde-cooperation-forward`、iptables 链
  `DDE-COOP-FWD` + marker 注释），装卸幂等，不碰他人规则。

## 拆除保证

| 场景 | 机制 |
| --- | --- |
| 会话断开 | `handleConnectStatus` → 控制器 deactivate（同步 D-Bus Teardown，通常毫秒级）→ 隧道流随 `onPeerDisconnected` 消亡 |
| 应用退出 | aboutToQuit → `stopCooperation` → `stopSubnetForward`；析构兜底 |
| 应用崩溃 | 助手监视申请方总线名，`NameOwnerChanged` 立即拆规则 |
| 助手崩溃 | 助手重启时 startupCleanup 清残留；应用 15s 租约续期（Setup 幂等重建）连续失败 2 次自动回直连 |
| SIGTERM/SIGINT | 助手信号处理 → teardownNow → 退出 |
| 助手缺失/拒绝 | 激活回滚（代理关闭、状态复位）；续期失败回直连 |

## 审计日志（WARNING 级，应用日志可见）

- 接受/拒绝动态 OPEN：流号、对端、`dst:port`、原因
- 代理转发/拒绝：原目标、原因
- 流关闭：对端、目标、双向字节量（`to peer` / `to target`）
- 控制器：激活（对端、网段、代理端口）、去激活（原因）
- 助手：Setup（申请方、后端、网段）、Teardown、启动清理

## 限制

- 仅 TCP；UDP/DNS 不覆盖（文档与日志均已明示）。
- 助手按 polkit 动作授权：`cn.uniontech.cooperation.forward.{setup,teardown}`，
  活动会话免密（`allow_active=yes`），其余 `auth_admin`；助手不交互授权
  （flags=0，fail-closed）。本地恶意用户若获授权可将流量重定向到自己的
  监听口——与任何本地透明代理一致，属既定信任边界。

## 真机回归清单（单向网络：A=10.8.12.90 → B=10.8.15.x 单向可达）

前置：两机装包（helper、dbus service、polkit policy 就位）；按上文示例配置；
`systemctl` 无需操作（D-Bus 激活）。

1. **基本连通**：A 主动连 B 建立协同（barrier）。B 上
   `curl -m5 http://10.8.12.5:PORT`、`ssh 10.8.12.5`（非 A 本机）连通；
   `ss -tni dst 10.8.12.5` 无 B→10.8.12.5 直连条目，且
   `ss -tni dst 10.8.12.90:51598` 的 bytes_sent/bytes_acked 增长量与
   传输量吻合 → 流量走会话。
2. **规则就位**：B 上 `nft list table ip dde-cooperation-forward` 存在，
   loopback 与 51598 return 在 redirect 之前；A 上无任何本功能 nft 表。
3. **会话断开 ≤3s 拆除**：B 侧断开协同（或 A 掉线），计时
   `nft list table ip dde-cooperation-forward` 消失 ≤3s；随后 B `curl`
   10.8.12.x 按原网行为失败（直连不可达），无流量黑洞。
4. **反复断连/重连/退出无残留**：连续 5 轮断开-重连；两机各执行一次退出
   与 `kill -9` 应用；`nft list tables | grep dde-cooperation` 与
   `iptables -t nat -S | grep DDE-COOP` 均无残留；`ss -tlnp | grep
   <proxyPort>` 无残留监听（应用退出后）。
5. **并存互不影响**：子网转发激活期间，barrier 键鼠共享正常、
   B→A 文件传输成功且速率无异常劣化；关闭子网转发（配置改 Enabled 后
   保存）≤ 数秒内回直连且 barrier 保持。
6. **限额**：配置 `MaxBytesPerSecond=1048576`，B 拉大文件实测吞吐 ≈ 上限；
   `IdleTimeoutMs=5000`，空闲连接 5s 后两侧流关闭（应用日志 `idle timeout`）。
7. **安全路径**：B 伪造 `dst=10.8.12.90:51598` 的 OPEN（无现成 UI，测试
   工具注入）被拒（`port denied`）；未配置网段目标被拒
   （`network not allowed`）；`MaxStreams=1` 时第二路流被拒
   （`too many streams`）。日志均有 WARNING 审计行。
8. **老对端兼容**：一端升级本版、对端保持老版本：升级端行为与升级前一致
   （无新表、无新监听），协同/传输功能正常；协商位被忽略。
9. **未配置即不变**：两机均不写 `SubnetForward` 组，全量协同/传输回归与
   基线一致；`nft list tables` 无新表。

## 单元测试

- `tests/manager/tunnelmanager_test.cpp`：动态目标 OPEN 校验（命中/不命中
  网段、非法目标、未认证/未装策略）、限额（并发/带宽/空闲/拨号超时）、
  拆除与兼容（静态服务忽略 dst）。
- `tests/manager/subnetforward_test.cpp`：规则文本生成（环回排除先于
  redirect、marker、幂等装卸）、透明代理往返与二道门、能力位往返与老
  对端兼容、策略校验 fail-closed。
- `tests/coop/subnetforward_controller_test.cpp`：激活/回滚/租约丢失回直连。
- `tests/coop/subnetforwardconfig_test.cpp`：三层开关、白名单语义、端口
  解析、跳板防护、运行时变更信号。
