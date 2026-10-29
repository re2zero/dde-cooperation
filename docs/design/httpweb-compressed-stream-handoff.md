+++
id = "httpweb-compressed-stream-handoff-20261029"
title = "httpweb 压缩归档流会话交接协议"
status = "active"
doc_version = "0.1"
audience = ["developers", "agents"]
last_updated = "2026-10-29"
tags = ["handoff", "httpweb", "implementation", "session", "zstd"]
related_docs = [
    "docs/design/httpweb-compressed-stream-research.md",
    "docs/design/httpweb-compressed-stream-implementation-plan.md"
]
+++

# httpweb 压缩归档流会话交接协议

> 这是下一个实施会话的入口文档。**先读本文，再读调研报告第 2–4 章，最后读实施计划。**
> 本文是“当前状态/下一步”的唯一事实来源；任务细节以实施计划为准，设计原理以调研报告为准。

## 1. 文档职责与阅读顺序

| 文档 | 职责 | 阅读时机 |
|---|---|---|
| 本文（handoff） | 当前状态、决策、下一步、交接规则 | 每个会话开始和结束 |
| [调研报告](httpweb-compressed-stream-research.md) | 问题、现状、分析、协议/线程/兼容方案 | 开始实施前通读第 2–4 章 |
| [实施计划](httpweb-compressed-stream-implementation-plan.md) | WBS、任务验收、排期、测试命令 | 领取任务时查对应 Phase |

**冲突处理：**
- 代码行为与文档不一致时，以“实际代码 + 测试”为准，并立即更新文档。
- 任务状态冲突时，以本文状态表 + `git status` 为准；不要假设上一个会话完成了未记录的工作。
- 决策冲突时，以第 3 节 Decision Log 为准；修改决策必须经用户确认并更新三份文档。

## 2. 当前快照（2026-10-29）

| 项 | 状态 |
|---|---|
| 分支 | `v20-trans` |
| 代码改动 | **无**；仅新增 `docs/design/` 下文档 |
| Git 状态 | `?? docs/design/`（未提交） |
| 设计状态 | 已评审，D1–D10 冻结 |
| 实施状态 | 未开始（M0 完成，Phase 0 未开始） |
| 下一任务 | `P0-1` 基准工具与数据集生成器 |
| 开发机 Qt | 仅 Qt5（`/usr/lib/x86_64-linux-gnu/cmake/Qt5`） |
| 构建约束 | C++17；Qt5/Qt6；Linux/Windows/Android |
| zstd 决策 | 新版默认依赖并默认使用；deflate 永久回退；协议协商 codec |

## 3. 已冻结决策（摘要）

| ID | 决策 |
|---|---|
| D1 | 单连接、自描述帧、整流传压缩的 DTP2 归档流 |
| D2 | JWT `dtp` claim 声明版本/codec；接收端在 stream 请求中指定 codec |
| D3 | deflate 永久基线；新版默认依赖并使用 zstd（优先静态链接） |
| D4 | 旧端 legacy；新服务端保留 `info`/`download` |
| D5 | v1 `Connection: close`；chunked 作为 v2 |
| D6 | v1 整文件粒度断点；v2 文件内偏移 |
| D7 | `FileServer`/`FileClient` 公开 API 不变 |
| D8 | IO 线程只收发；遍历/压缩/解压/写盘在生产者/消费者线程 |
| D9 | 每文件 CRC32 trailer + END `streamCRC` + `.part` 原子改名 |
| D10 | 环境变量/特性开关可强制 legacy、指定 codec/level |

完整理由见调研报告 4.4 和 4.7；变更规则见实施计划第 2、10 节。

## 4. 下个会话启动清单

### 4.1 环境与状态确认

```bash
cd /home/ut003403@uos/work/repo/dde-cooperation
git branch --show-current        # 期望 v20-trans
git status --short               # 期望只有 docs/design/ 未跟踪
qmake -v                         # 当前机器是 Qt5
```

- 如果 `git status` 出现未知代码改动：先停下来核对，不要覆盖。
- 如果分支不是 `v20-trans`：先确认是否要切换；不要在新分支上直接实施。
- 如果已有 `build-dev/`：确认是不是上一会话的构建目录；不要直接删除，先看 CMakeCache。

### 4.2 基线构建

当前机器没有 Qt6，**不要直接运行 `test-prj-running.sh`**（它强制 Qt6，会配置失败）。

```bash
cmake -S . -B build-dev \
  -DCMAKE_BUILD_TYPE=Debug \
  -DDOTEST=ON

cmake --build build-dev -j"$(nproc)"
```

如果使用 Qt6 环境，追加：

```bash
-DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake/Qt6 -DQT_DESIRED_VERSION=6
```

### 4.3 基线测试

```bash
TEST_BIN="$(find build-dev -type f -executable -name lib_logic_tests | head -1)"
"$TEST_BIN" --gtest_list_tests
"$TEST_BIN" --gtest_filter='HttpWeb*'
```

- 先记录基线失败；不要为了“全绿”去改无关测试。
- netutil 的 Catch2 测试需要 `-DBUILD_NETUTIL_TESTS=ON`，部分用例可能挂起；用 `timeout`，日常门禁用 `netutil_http_tests`。

### 4.4 建立任务列表

把 Phase 0 的任务建进 `TodoWrite`，顺序建议：

1. `P0-1` 基准工具与数据集生成器（先拿基线）。
2. `P0-2` 并发/生命周期修复。
3. `P0-3` 超时/重连/请求隔离。
4. `P0-4` 服务端健壮性。
5. `P0-5` HTTP header/body 正确性。
6. `P0-6` 符号链接环检测。
7. `P0-7` baseline 记录。

> P0-1 和 P0-2 可以并行；如果只做单线程，先 P0-1 生成数据，再 P0-2 修复后重跑基线。

## 5. 首个任务建议（P0-1）

**目标：** 建立可重复的 legacy 基线，后续所有优化都以它为对照。

**要做：**

- 新增 `tests/benchmarks/` 与 `CMakeLists.txt`，用 `BUILD_HTTPWEB_BENCH=ON` 控制。
- 实现数据集生成：`--generate <dir> --files N --size S`，支持 1k/10k/100k/1M；目录树可配置深度。
- 实现 legacy 跑分：启动本地 `FileServer` + `FileClient`，或直接调用现有传输路径；带超时。
- 输出：文件数、总字节、耗时、CPU user/sys、RSS 峰值、请求数、最长无数据停顿、失败数。
- 结果先写进本文第 8 节日志；Phase 5 再整理进调研报告第 6 章。

**验收：**

- 100k × 4KB 数据集可在合理时间内生成。
- legacy 跑分可重复执行两次，结果偏差可解释。
- 超时/中断时进程能退出，不挂死。

## 6. 会话结束交接清单

每个会话结束前必须完成：

1. **更新状态表**（第 7 节）：任务状态、完成度、下一任务。
2. **追加会话日志**（第 8 节）：目标、完成、改动文件、测试命令/结果、阻塞、下一步。
3. **跑目标测试**：至少覆盖本次改动；记录原始命令和结果，不只写“通过”。
4. **保持树可构建**：如果不能，明确写出“WIP + 编译错误 + 下一行要改哪里”。
5. **不提交**：除非用户明确要求；`git status --short` 写进日志。
6. **不改冻结决策**：如确需变更，先更新 Decision Log、调研报告、实施计划，并请用户确认。
7. **记录环境异常**：Qt 版本、缺依赖、网络不可用、测试自身失败等。

## 7. 状态表

| 阶段 | 状态 | 完成度 | 下一任务 |
|---|---|---|---|
| M0 调研/设计/计划/交接 | 完成 | 100% | — |
| Phase 0 基准与稳定性 | 未开始 | 0% | P0-1 |
| Phase 1 编解码与帧 | 未开始 | 0% | P1-1 |
| Phase 2 发送端 | 未开始 | 0% | P2-1 |
| Phase 3 接收端 | 未开始 | 0% | P3-1 |
| Phase 4 背压/断点/故障 | 未开始 | 0% | P4-1 |
| Phase 5 打包/兼容/验收 | 未开始 | 0% | P5-1 |

**Phase 0 任务级状态：**

| ID | 任务 | 状态 | 备注 |
|---|---|---|---|
| P0-1 | 基准工具/数据集 | 未开始 | 首个任务 |
| P0-2 | 并发/生命周期修复 | 未开始 | |
| P0-3 | 超时/重连/请求隔离 | 未开始 | |
| P0-4 | 服务端健壮性 | 未开始 | |
| P0-5 | HTTP header/body 正确性 | 未开始 | |
| P0-6 | 符号链接环检测 | 未开始 | |
| P0-7 | legacy 基线记录 | 未开始 | 依赖 P0-1..P0-6 |

> 后续 Phase 的任务级状态在实施计划第 4 节；本文只维护阶段状态和当前 Phase 的任务状态，避免两份表漂移。

## 8. 会话日志

### 2026-10-29 — 调研、设计与交接

- 目标：完成 `httpweb` 调研、DTP2 方案、实施计划和会话交接。
- 完成：调研报告、五视角图表、实施计划、本交接协议。
- 改动：新增 `docs/design/httpweb-compressed-stream-research.md`、`...-implementation-plan.md`、`...-handoff.md`。
- 测试：未运行（仅文档）。
- 结果：设计冻结；无代码改动。
- 阻塞：无。
- 下一步：P0-1 基准工具。

### 会话日志模板

```markdown
### YYYY-MM-DD — <本次主题>

- 目标：
- 完成：
- 改动文件：
- 测试命令/结果：
- 阻塞/风险：
- 下一步（具体到任务 ID）：
- git status：
```

## 9. 已知坑与禁令

### 9.1 必须避免的实现坑

1. **不要在 Asio IO 线程调用 `Send(buffer, size, timeout)`**：它内部等自己的异步 handler，会死锁。IO 线程只做异步发送。
2. **END 入队后不要立刻 `Disconnect`**：先排空 `_send_buffer_main`/`_send_buffer_flush`，否则截断流。
3. **`bytes_pending()` 目前无锁读**：生产者跨线程使用前改成 atomic 或加锁。
4. **HTTP 无 `Content-Length` 时不要依赖 `\r\n\r\n` 结束**：stream 响应加 `X-DTP-Stream` 保护，END 帧判断结束。
5. **header/body 同包可能丢数据**：`onReceivedResponseHeader` 清理 cache 前要保留/转交 body 字节。
6. **`BaseKit::File` `buffer=0` 可能短写**：保留默认缓冲或自己循环写。
7. **`offset > size` 会 `Content-Length` 下溢**；`std::stoll` 可能抛异常。stream 端点必须校验/捕获。
8. **`HTTPFileClient::_canceled` 必须每请求重置**；`_callback` 弱指针和 `_status.path` 必须加锁/快照；`TransferWorker::_canceled` 改 atomic。
9. **`webBind` 失败不能继续把 name 放进 token/RPC**；`requestInfo` 失败不能等价于空文件。
10. **符号链接可能成环**：`FileSizeCounter` 和遍历都要 visited 集合。
11. **不要依赖 `directory_iterator` 顺序做断点**：每层排序。
12. **不要直接写最终文件名**：`.part` + CRC/大小校验 + rename。
13. **失败不要发 `WEB_TRANS_FINISH`**：发 `WEB_IO_ERROR`/`WEB_DISCONNECTED`。
14. **不要在 stream 失败后混用 legacy**：已写文件后只 retry/resume，否则产生重复目录。

### 9.2 环境/测试坑

- 当前机器只有 Qt5；`test-prj-running.sh` 强制 Qt6，不能作为日常门禁。
- 项目根 `CMakeLists.txt` 默认 `DOTEST=OFF`；必须显式 `-DDOTEST=ON`。
- `tests/logic` 使用 GTest；`lib_logic_tests` 链接 `httpweb`，新增测试文件放 `tests/logic/*_test.cpp` 会被 glob。
- netutil 的 Catch2 通过 Gitee FetchContent 获取，网络不可用时不要阻塞；优先 `netutil_http_tests`。
- 已知部分测试会挂起/失败（见 `test-prj-running.sh` 注释）；不要把它们当成新回归。

### 9.3 禁令

- 不修改 `FileServer`/`FileClient` 公开方法签名；不删除 legacy 端点。
- 不把 UI 线程/业务层拖进 stream 细节。
- 不在 IO 线程读盘、写盘、压缩、解压、sleep。
- 不提交 `build*/`、生成文件、测试数据集。
- 不记录 token/密钥/用户文件内容到日志。
- 不未经用户确认改变 D1–D10。

## 10. 代码地图

| 区域 | 路径 | 说明 |
|---|---|---|
| 传输库 | `src/lib/common/httpweb/` | 主要改动区；新增 stream* / checkpoint |
| 服务端 | `src/lib/common/httpweb/fileserver.cpp` | `serveStream`、HTTP 响应、producer 启动 |
| 客户端 | `src/lib/common/httpweb/fileclient.cpp` | capability、stream 下载、路径映射、回退 |
| Token | `src/lib/common/httpweb/tokencache.cpp` | `dtp` claim |
| 协议结构 | `src/lib/common/httpweb/syncstatus.h` | 状态/回调；尽量不改枚举 |
| 网络栈 | `src/infrastructure/netutil/src/asio/tcp_session.cpp` | PauseReceive、send drain、atomic stats |
| HTTP 解析 | `src/infrastructure/netutil/src/http/http_response.cpp` | 流式 body/结束判断 |
| 业务衔接 | `src/lib/common/manager/transferworker.cpp` | 回调、并发、错误传播 |
| 逻辑测试 | `tests/logic/` | 新增 `httpweb_stream_*_test.cpp` |
| 基准 | `tests/benchmarks/` | 新增目标，`BUILD_HTTPWEB_BENCH=ON` |
| Linux 打包 | `debian/control` | 加 `libzstd-dev` |
| Windows 打包 | `vcpkg.json.in`、`clean_build.bat`、`prepare_dependencies.bat` | zstd 静态链接 |
| Android | `android/modules/cooperation/src/main/cpp/CMakeLists.txt` | zstd 预编译/链接 |

## 11. 开放问题（不阻塞 Phase 0）

| 问题 | 默认处理 | 何时决定 |
|---|---|---|
| Android zstd 来源/ABI | 优先 NDK/项目预编译；不行则 vendored 源码 | Phase 5 P5-1 |
| Windows zstd 静态三元组 | vcpkg `zstd:x64-windows-static` | Phase 5 P5-1 |
| 符号链接：跟随还是保留 | 跟随（与现状一致）+ 环检测；保留作为后续增强 | Phase 2 P2-1 |
| mode/mtime 是否默认保留 | v1 可选，不阻塞 | Phase 3 P3-4 |
| 单文件选择是否走 stream | 先按“目录/multi-select 走 stream；单文件可 legacy 或 codec=none”验证 | Phase 3 集成后 |
| 文件内偏移续传 | v2；v1 整文件 | Phase 4 P4-3 |
| UI 总进度 | 复用现有 RPC total，不改 UI | 全程 |

## 12. 复制即用的下会话提示词

```text
请先阅读并遵循：
1. docs/design/httpweb-compressed-stream-handoff.md
2. docs/design/httpweb-compressed-stream-research.md 第 2–4 章
3. docs/design/httpweb-compressed-stream-implementation-plan.md

当前分支 v20-trans，当前机器是 Qt5。从 Phase 0 的 P0-1（基准工具与数据集生成器）开始，随后 P0-2..P0-6。
每完成一个任务：更新 handoff 第 7 节状态表和第 8 节日志，记录测试命令/结果。
不要修改 FileServer/FileClient 公开 API；不要删除 legacy 端点；不要阻塞 Asio IO 线程；不要提交构建产物。
```

## 13. 交接协议变更控制

1. 本文的“状态表/会话日志”每个会话都可更新；“决策/禁令/开放问题”变更需用户确认。
2. 如果实施计划新增/删除任务，先改计划 WBS，再在本文状态表加一行。
3. 如果某个决策被推翻，必须同时更新：本文第 3 节、调研报告 4.4/4.7、实施计划第 2 节。
4. 会话被中断时，下一会话以本文最后一条日志 + `git status` 为准；不要假设未记录的工作已完成。
