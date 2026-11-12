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

## 2. 当前快照（2026-11-12，真机验证后）

| 项 | 状态 |
|---|---|
| 分支 | `v25-trans`（文档旧快照写的是 `v20-trans`，以实际 git 为准） |
| 代码改动 | Phase 0–5 主体完成；Android APK 已真机安装、扫码并投屏成功；根内符号链接保留/根外跳过与 Symlink 帧已完成（待提交） |
| Git 状态 | 已提交 7 个代码/文档提交（未推送）；本轮 Symlink 帧、walker preserve 模式、测试与三份文档改动未提交 |
| 设计状态 | 已评审，D1–D12 冻结 |
| 实施状态 | Phase 0–5 完成（P5-1/P5-3/P5-4/P5-5）；P4-5 部分；真实目录 `~/work` 全树 + 链接还原验收完成 |
| 下一任务 | Windows 实机验证；P5-2 旧二进制矩阵；P4-5 慢盘/磁盘满/停止专项；两台真实 GUI 应用之间的文件传输 |
| 开发机 Qt | 实际为 Qt6（`/usr/lib/qt6`）；文档旧快照写的 Qt5 已过时 |
| GTest | 已安装 1.12.1；`-DDOTEST=ON` 可用；当前 343 项逻辑测试全绿 |
| zlib / zstd | zlib 1.3.2、zstd 1.5.7，CMake config 可用，静态库可链接进共享库 |
| 构建约束 | C++17；Qt5/Qt6；Linux/Windows/Android |
| zstd 决策 | 新版默认依赖并默认使用；deflate 永久回退；协议协商 codec |

## 3. 已冻结决策（摘要）

| ID | 决策 |
|---|---|
| D1 | 单连接、自描述帧、整流传压缩的 DTP2 归档流 |
| D2 | JWT `dtp` claim 声明版本/codec；接收端在 stream 请求中指定 codec |
| D3 | deflate 永久基线；新版默认依赖并使用 zstd（Linux 共享库、Windows/Android 静态） |
| D4 | 旧端 legacy；新服务端保留 `info`/`download` |
| D5 | v1 `Connection: close`；chunked 作为 v2 |
| D6 | v1 整文件粒度断点；v2 文件内偏移 |
| D7 | `FileServer`/`FileClient` 公开 API 不变 |
| D8 | IO 线程只收发；遍历/压缩/解压/写盘在生产者/消费者线程 |
| D9 | 每文件 CRC32 trailer + END `streamCRC` + `.part` 原子改名 |
| D10 | 环境变量/特性开关可强制 legacy、指定 codec/level |
| D11 | 生产默认开启 `TCP_NODELAY`（服务端与客户端）；基准工具可显式覆盖以复现旧基线 |
| D12 | 新接收端通过 `links=1` 协商后，根内符号链接以 Symlink 帧保留并还原（目标改写为相对链接父目录），根外/不可解析跳过；未协商的旧接收端保持跟随+实体化；旧跟随模式按当前路径祖先链检测环，不用全局 inode 去重 |

完整理由见调研报告 4.4 和 4.7；变更规则见实施计划第 2、10 节。

## 4. 下个会话启动清单

### 4.1 环境与状态确认

```bash
cd /home/ut003403@uos/work/repo/dde-cooperation
git branch --show-current        # 期望 v25-trans
git status --short               # 应当能看到本次会话的未提交改动
qmake -v                         # 本机 qmake 未配置；实际使用 Qt6（/usr/lib/qt6）
```

- 如果 `git status` 出现未知代码改动：先停下来核对，不要覆盖。
- 如果分支不是 `v25-trans`：先确认是否要切换；不要在新分支上直接实施。
- 如果已有 `build-dev/`：确认是不是上一会话的构建目录；不要直接删除，先看 CMakeCache。
- 本机没有 GTest：不要用 `-DDOTEST=ON`，见 9.2。

### 4.2 基线构建

```bash
cmake -S . -B build-core -DCMAKE_BUILD_TYPE=Debug

cmake --build build-core -j"$(nproc)"
```

快速验证传输库：

```bash
cmake --build build-core -j"$(nproc)" --target httpweb
```

如果使用带 GTest 的环境，可追加 `-DDOTEST=ON` 并构建 `lib_logic_tests`。Qt6 环境无需额外参数；Qt5 环境追加 `-DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake/Qt5 -DQT_DESIRED_VERSION=5`。

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
| Phase 0 基准与稳定性 | 完成 | 100% | — |
| Phase 1 编解码与帧 | 完成 | 100% | — |
| Phase 2 发送端 | 完成 | 100% | — |
| Phase 3 接收端 | 完成 | 100% | — |
| Phase 4 背压/断点/故障 | 进行中 | 80% | P4-5 |
| Phase 5 打包/兼容/验收 | 进行中 | 70% | P5-1/P5-2 |

**Phase 0 任务级状态：**

| ID | 任务 | 状态 | 备注 |
|---|---|---|---|
| P0-1 | 基准工具/数据集 | 完成 | `tests/benchmarks/`，`-DBUILD_HTTPWEB_BENCH=ON`；见 7.2 |
| P0-2 | 并发/生命周期修复 | 完成 | `_canceled` atomic；`_status` 加锁快照；`WebInterface::callback()` 加锁；`HTTPFileClient::_handler` 互斥快照；`_canceled` 每请求重置 |
| P0-3 | 超时/重连/请求隔离 | 部分 | 超时后主动断开并等待；handler 快照隔离旧回调。缺：显式连接重建与跨文件错位的专项测试 |
| P0-4 | 服务端健壮性 | 完成 | `offset > size` 返回 416；`stoll` 捕获；`webBind` 失败中止传输；`requestInfo` 返回 bool；`downloadFile` 返回真实结果；非普通文件返回 404 |
| P0-5 | HTTP header/body 正确性 | 完成 | 新增 `ConsumeHeader`；`X-DTP-Stream` 检测与 CRLFCRLF 保护；6 项单测 |
| P0-6 | 符号链接环检测 | 完成 | `FileSizeCounter` 用 `(dev,inode)` visited + 迭代遍历；客户端遍历深度上限 64 |
| P0-7 | legacy 基线记录 | 完成 | 1k/10k 实测 + 100k 运行中；见 7.2 |

**Phase 1 任务级状态：**

| ID | 任务 | 状态 | 备注 |
|---|---|---|---|
| P1-1 | `StreamCodec` | 完成 | none/deflate/zstd，分块 feed/flush/finish，输出上限 |
| P1-2 | `StreamFrame` | 完成 | 帧头/CRC/END/前导，增量解析，路径校验 |
| P1-3 | 单元测试 | 完成 | 41 项：逐字节截断、CRC 翻转、超长/穿越路径、空文件、Unicode、10 万文件往返 |
| P1-4 | CMake | 完成 | `ZLIB::ZLIB` 必需；zstd 可选，`HTTPWEB_HAVE_ZSTD` 宏；Linux 共享、Windows/Android 静态（见 7.3） |
| P1-5 | 接口注释 | 完成 | 头文件内说明线程约束、错误语义、线格式 |

**Phase 2 任务级状态：**

| ID | 任务 | 状态 | 备注 |
|---|---|---|---|
| P2-1 | `StreamWalker` 确定性遍历 | 完成 | 每层字节序排序；显式栈；preserve 模式发 Symlink 帧/根外跳过；旧模式根内跟随+祖先链环检测；特殊文件/不安全名/深度上限跳过并上报；19 项测试 |
| P2-2 | `StreamProducer` | 完成 | 前导→遍历→帧→压缩→sink；每文件 flush；取消/文件缩短/打开失败语义；事件回调；8 项测试 |
| P2-3 | `serveStream` 端点 | 完成 | `GET stream/<b64>&token&codec&level&from&off`；无 Content-Length、`Connection: close`；from/off≠0 返回 501；新增 `SetOpenEndedBody()` |
| P2-4 | 异步发送与背压 | 完成 | `SendResponseBodyAsync` + 2MB 高水位；END 后等待发送队列排空再 `Disconnect`；生产者线程由 FileServer 持有并在 stop() join |
| P2-5 | 进度/取消/错误映射 | 完成 | FileBegin/Progress/End → `RES_OKHEADER/RES_BODY/RES_FINISH`（post 到 IO 线程）；ERROR 帧仅记录不中断；单文件不可读不拖垮整条流 |
| P2-6 | 发送端集成测试 | 完成 | 真实 TLS 客户端读 preamble/帧/END；目录树+zstd、单文件+deflate、坏 codec、断点、缺 token 共 5 项 |

**Phase 3 任务级状态：**

| ID | 任务 | 状态 | 备注 |
|---|---|---|---|
| P3-1 | token 能力协商 | 完成 | `genToken` 写入 `dtp` claim（`{"v":2,"codecs":[zstd,deflate]}`）；`getDtpFromToken` 解析；无 claim 直接 legacy；`DDE_COOPERATION_HTTPWEB_TOKEN_DTP=0` 可回滚；服务端仍逐请求验签 |
| P3-2 | `StreamConsumer` | 完成 | 解压/解析/落盘一体；16KB 解压切片限内存；10 项测试 |
| P3-3 | 路径映射/冲突/安全 | 完成 | 根目录只建一次并按 web 名命名；冲突 `(1)`；`..`/绝对路径拒绝；跨 symlink 拒绝；归档内重复名不覆盖；Symlink 帧目标词法+canonical 双重校验后还原 |
| P3-4 | `.part`/CRC/rename | 完成 | 边写边由解析器校验每帧 CRC；成功后 rename；失败/取消删除 `.part` |
| P3-5 | 进度/错误/legacy 回退 | 完成 | 流式进度映射 `WEB_FILE_*`/`onProgress`；404/非流响应在写盘前回退 legacy；`DDE_COOPERATION_HTTPWEB_STREAM=0` 可关闭 |
| P3-6 | 接收端集成测试 | 完成 | HTTP 单文件往返 + 消费者树/损坏/截断/穿越/重名；100k A/B 逐文件校验一致 |
| P3-7 | 内部接口注释 | 完成 | 头文件说明线程、失败和路径约束 |

**Phase 4 任务级状态：**

| ID | 任务 | 状态 | 备注 |
|---|---|---|---|
| P4-1 | `PauseReceive`/`ResumeReceive` | 完成 | `SSLSession` 与 `SSLClient` 均实现；4MB 高水位暂停、1MB 低水位恢复；恢复 post 到 io_service/strand |
| P4-2 | 发送缓冲排空/关闭 | 完成 | END 后 `waitStreamDrained(60s)` 再关闭；故障测试验证截断时不误报成功 |
| P4-3 | checkpoint/from/off | 完成 | 每帧回调、1s 节流写盘、失败立即写；token+name 哈希定位；临时文件+原子 rename；损坏/丢失自动回退；`from=N` 按帧跳过；`off>0` 仍 501；源树变化未做 size/mtime 校验（v1 限制） |
| P4-4 | watchdog/重试/错误分类 | 完成 | 空闲 60s（`DDE_COOPERATION_HTTPWEB_STREAM_IDLE_TIMEOUT` 可调）断开；最多 3 次、200/800ms 退避；磁盘/路径/协议错误不重试；续传冲突自动换新根重来 |
| P4-5 | 故障注入测试 | 部分 | TCP 代理“中途切断”“挂起不转发”两条用例通过；慢盘、磁盘满、用户停止专项待补 |

**Phase 5 任务级状态：**

| ID | 任务 | 状态 | 备注 |
|---|---|---|---|
| P5-1 | zstd 链接/打包 | 部分 | Linux：`libzstd-dev` 加入 Build-Depends 与 dev 包依赖，运行时由 `${shlibs:Depends}` 带 `libzstd1`；Windows：vcpkg manifest 加 `zstd`，三元组改为 `*-windows-static-md`（未实机验证）；Android：NDK 27 交叉编译 zstd 1.5.5（arm64/armv7），`libcooperation.so` 与 debug APK 构建成功，`ZSTD_*` 已静态链接 |
| P5-2 | 新旧兼容矩阵 | 部分 | 旧 token、旧客户端行为、服务端关闭 stream（运行中 404 与启动时省略 claim）四条兼容用例通过；真正的旧版本二进制全矩阵待环境 |
| P5-3 | 性能验收 | 完成 | 100k 2.58 s；1M×4KB（4 GB）58.5 s、RSS 56.5 MB；10 GB 单文件 42.3 s、RSS 20.0 MB；均逐文件校验一致 |
| P5-4 | 灰度/开关/回滚 | 部分 | 客户端 `DDE_COOPERATION_HTTPWEB_STREAM=0`、服务端 `DDE_COOPERATION_HTTPWEB_SERVER_STREAM=0`、token `DDE_COOPERATION_HTTPWEB_TOKEN_DTP=0` 三个开关；服务端 404 或 token 无 claim 时自动回退；故障注入下的自动回退演练未做 |
| P5-5 | 文档收尾 | 完成 | 调研报告 §6.3/§6.4、交接文档 §7.4/§7.6（能力协商、兼容矩阵、发布/回滚清单）已补齐；Windows/Android 实机结果待回填 |

### 7.1 Phase 1 线格式实现细节（Phase 2/3 必须遵循）

实现文件：`src/lib/common/httpweb/streamcodec.{h,cpp}`、`streamframe.{h,cpp}`。

**前导（未压缩，8 字节）：** `"DTP2" | version(1) | codec(1) | flags(1) | reserved(1)`。
前导不计入 `streamCRC`。

**帧头（小端）：** `type(1) | flags(1) | reserved(2) | pathLen(4) | path(N) | size(8) | offset(8) | mode(4) | mtime(8)`。
注意 path 在固定字段中间：前缀 8 字节 + path + 尾部 28 字节，不是连续的 36 字节块。解析器必须按两段读。

**载荷：**
- `RootDir`/`Dir`：无载荷，`size/offset` 必须为 0。
- `RootFile`/`File`：`size` 为本帧数据长度，后跟 `crc32(data)`（4 字节）。空文件也要带 CRC=0。
- `Error`：`size` 为消息长度，path 为出错路径（可为空、可为不安全路径，仅诊断用），无 CRC。
- `End`：path 必须为空，`size/offset` 必须为 0，后跟 20 字节 `entryCount(8) | totalBytes(8) | streamCRC(4)`。

**计数语义：** `entryCount` = Root*/Dir/File 帧数（不含 Error/End）；`totalBytes` = 各帧 `size` 之和；`streamCRC` 覆盖前导之后、END 之前的所有未压缩帧字节（含帧头、path、数据、CRC 尾部、ERROR 帧）。

**路径规则（`isSafeRelativePath`）：** 非空、无 NUL、非绝对路径、无 `\`、段非空且不为 `.`/`..`、控制字符拒绝；Windows 额外拒绝 `:<>"|?*`。根帧 path 必须是单段名。

**续传（v1）：** `GET stream/...&from=N` 跳过前 N 个已确认帧；接收端 checkpoint 记录 `from` 与 `rootPath`，恢复流不再带 RootDir/RootFile，接收端在现有根内继续；END 的 entryCount/totalBytes/streamCRC 只覆盖本次剩余帧；`off>0` 仍返回 501。源树在两次尝试之间变化可能造成帧索引错位，v1 不做 size/mtime 校验，检测到冲突时应整树重传。

**API 要点：**
- `Codec` 枚举值即线格式 ID：none=0/deflate=1/zstd=2。
- `StreamEncoder::flush()` = zlib `Z_SYNC_FLUSH` / zstd `ZSTD_e_flush`，每个文件结束调用，接收端可及时完成该文件。
- `FrameWriter` 自动累计 `entryCount/totalBytes`，`writeEnd()` 用它写 END；`streamCrc()` 在 END 之后保持 END 前的值。
- `FrameParser` 为增量解析，文件数据只经回调透传、不落内存；默认校验文件 CRC、流 CRC 和 END 计数。

### 7.2 P0-1 基准工具与 P0-7 legacy 基线

**构建与运行**

```bash
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DBUILD_HTTPWEB_BENCH=ON
cmake --build build-bench -j"$(nproc)" --target httpweb_transfer_benchmark

# 数据集（100k x 4KB）
./build-bench/tests/benchmarks/httpweb_transfer_benchmark \
  --generate /tmp/dtp-100k --files 100000 --size 4096 --dirs 256 --depth 2

# legacy 基线（进程内 FileServer + FileClient，loopback）
./build-bench/tests/benchmarks/httpweb_transfer_benchmark \
  --legacy --src /tmp/dtp-100k --dst /tmp/dtp-100k-out --timeout 5400
```

数据集参数：`--files/--size/--dirs/--depth/--pattern text|random|zero/--single 10G`。
跑分参数：`--repeat N`（同进程重复）、`--no-delay`（服务端开 TCP_NODELAY）、`--no-verify`。
输出为 `key=value`，包含 elapsed、CPU user/sys、RSS 峰值、最长停顿、HTTP 请求数/连接数、每文件传输与周期、逐文件 CRC32 校验结果。

**基线（loopback、单机、Qt6、4KB 文本文件、路径逐文件 CRC 校验通过）**

| 场景 | 文件数 | 耗时 | 每文件周期 | 每文件传输 | CPU user/sys | RSS 峰值 | 最长停顿 | 请求数 |
|---|---|---|---|---|---|---|---|---|
| 1k，按现状 | 1 000 | 44.8 s | 44.8 ms | 40.7 ms | 1.31/0.44 s | 12.6 MB | 57 ms | 3 HEAD + 22 info + 1000 download |
| 1k，`--no-delay` | 1 000 | 4.44 s | 4.38 ms | 0.021 ms | 1.12/0.24 s | 12.9 MB | 56 ms | 同上 |
| 10k，按现状 | 10 000 | 448 s | 44.8 ms | 40.7 ms | 13.7/4.5 s | 14.0 MB | 233 ms | 3 HEAD + 74 info + 10000 download |
| 100k，按现状 | 100 000 | 4 484 s | 44.7 ms | 40.6 ms | 135.3/43.4 s | 22.8 MB | 12.2 s | 3 HEAD + 274 info + 100000 download |

线性度成立（1k/10k/100k 的每文件周期为 44.7–44.8 ms），100k 实测 4 484 s，与线性预估一致；最长停顿放大到 12.2 s。

**改造后（stream）与对照（同机、Release、独立端口、`verifyTrees` 通过）**

| 场景 | 文件数 | 耗时 | 每文件周期 | CPU user/sys | RSS 峰值 | 最长停顿 | 请求数 |
|---|---|---|---|---|---|---|---|
| stream | 1 000 | 0.057 s | 0.027 ms | 0.037/0.030 s | 17.8 MB | 20 ms | 3 HEAD + 1 download |
| legacy `--no-delay` | 1 000 | 4.65 s | 4.56 ms | 1.33/0.23 s | 18.9 MB | — | 1000 download + 40 info |
| legacy 按现状 | 1 000 | 44.9 s | 44.8 ms | 1.46/0.45 s | 12.9 MB | — | 1000 download + 40 info |
| stream | 10 000 | 0.275 s | 0.024 ms | 0.247/0.276 s | 20.4 MB | 20 ms | 3 HEAD + 1 download |
| legacy `--no-delay` | 10 000 | 45.59 s | 4.56 ms | 12.59/2.60 s | 14.0 MB | — | 10000 download + 74 info |
| legacy 按现状 | 10 000 | 447.98 s | 44.8 ms | 14.10/4.47 s | 14.0 MB | 233 ms | 10000 download + 74 info |
| stream | 100 000 | 2.50 s | 0.025 ms | 2.40/2.76 s | 21.4 MB | 27 ms | 3 HEAD + 1 download |

**收益拆分：** 10k 上 stream 比 legacy 快 1629×，比“拿走 Nagle 的 legacy”仍快 166×；CPU 从 18.6 s 降到 0.52 s。主要收益是 10 000 次 HTTP/TLS 事务变成 1 次；压缩是第二位的。代价是 RSS +6.4 MB（zstd 上下文 + 4 MB 接收队列高水位 + 解压缓冲）。1k/10k/100k 的 stream 每文件周期都稳定在 0.024–0.027 ms。

**基准运行注意：** 基准默认 `SetupReusePort(true)`。并行或连续跑多个基准时必须给每个进程不同的 `--port`，否则连接可能落到上一进程的 listener，表现为偶发 404/回退。改造后使用 `--stream`；`--legacy` 会显式设置 `DDE_COOPERATION_HTTPWEB_STREAM=0`，保证测的是旧路径。

**关键发现**

1. **每文件约 40 ms 是 Nagle + delayed ACK，不是协议本身。** `TCPServer` 的 `_option_no_delay` 默认 false，`FileServer` 从未调用 `SetupNoDelay`；`serveContent` 先发响应头、再发首块 body 是两次小写，第二段要等对端 ACK（对端延迟 ACK ~40 ms）。服务端开启 `TCP_NODELAY` 后，同一路径 1k 文件从 44.8 s 降到 4.44 s（10.1x），每文件传输时间从 40.7 ms 降到 0.021 ms。
2. 去掉 Nagle 后仍有 **约 4.4 ms/文件** 的固定成本：逐文件请求、`createNextAvailableName`+创建/打开/关闭、以及两处 `setResponseHandler` 的 `Yield+Sleep(1)`。
3. CPU 只占 4%（10k：18.2 s / 448 s），瓶颈是等待不是计算；RSS 不随文件数增长。
4. 请求数随文件数线性增长：每个非空文件 1 次 `download`，每个目录 1 次 `info`（根目录 2 次），加 3 次 HEAD。

**已落地（2026-11-03，D11）**

- `FileServer::start()` 调用 `SetupNoDelay(true)`；`FileClient` 创建 HTTP 客户端后同样开启。legacy 回退路径与 HEAD/info 请求都受益。
- 基准工具在 `start()` 之后显式覆盖该选项：不带 `--no-delay` 仍可复现 `nodelay=0` 的历史基线（实测 1k：44.84 s）；带 `--no-delay` 为 4.47 s。
- 即使开启 no_delay，每文件仍有约 4.4 ms 的请求与文件系统成本；DTP2 去掉 `N_f × RTT`、逐文件 fs 与压缩收益的核心目标不变。

### 7.3 zstd 链接策略（D3 定稿：Linux 共享，Windows/Android 静态）

D3 写的是“优先静态链接”。实测发现这在 Linux 上会把符号泄漏进共享库：

- `libzstd.a` 被静态链接进 `libcooperation-core.so` 后，`ZSTD_*`/`HUF_*` 成为该 `.so` 的导出符号。
- 同一进程里另一个依赖加载了系统的 `libzstd.so.1`（本机由 `libsystemd` 带入），动态链接器会用它插入（interpose）这些符号。
- 结果是 `ZSTD_createDCtx` 与 `ZSTD_decompressStream` 落在两个不同副本上，上下文布局不一致，进程在 `HUF_readDTableX2_wksp` 段错误。官方 `lib_logic_tests` 的 10 万文件 zstd 往返用例复现，ASan/独立链接都不复现，只有“共享库 + 两份 zstd”时才出现。

现行实现：**Linux 优先共享库**（`zstd::libzstd_shared`，包依赖 `libzstd1`）；**Windows/Android 仍静态**（安装包/APK 不带额外 DLL/SO）。这既避开符号插入，也保留移动端/Windows 的零部署优势。

> 已于 2026-11-03 经用户确认：D3 改为“Linux 共享库、Windows/Android 静态”。同步更新本文件 §3、调研报告 §4.4/§6、实施计划 §2。

### 7.4 JWT `dtp` 能力协商（2026-11-03）

新服务端在 token 中增加字符串 claim：

```json
{"v":2,"codecs":["zstd","deflate"]}
```

未编译 zstd 的构建只声明 `deflate`。客户端 `TokenCache::getDtpFromToken()` 只把 claim 当作提示（不验签）；无 claim / 版本 <2 / JSON 损坏时直接 legacy，有 claim 时按交集选择 codec 并在 `stream/...&codec=` 中指定。服务端每次请求仍验证 JWT 签名。

`DDE_COOPERATION_HTTPWEB_TOKEN_DTP=0` 让 `genToken` 生成旧形状 token，用于回滚和兼容演练。

### 7.5 Android 交叉编译与 APK（2026-11-03）

环境：Android SDK 34 + NDK 27.0.11902837 + CMake 3.22.1（SDK 自带 Ninja）；OpenSSL 1.1.1w 与 zstd 1.5.5 均用 NDK 从源码交叉编译，覆盖 `arm64-v8a` 和 `armeabi-v7a`。

**预编译布局：**

- `$ANDROID_HOME/android_openssl/ssl_1.1/{arm64-v8a,armeabi-v7a}/libcrypto_1_1.so`、`libssl_1_1.so`，头文件在 `ssl_1.1/include/openssl`。
- `$ANDROID_HOME/android-zstd/include/{zstd.h,zstd_errors.h,zdict.h}`。
- `$ANDROID_HOME/android-zstd/{arm64-v8a,armeabi-v7a}/lib/libzstd.a`。
- `$ANDROID_HOME/android-zstd/lib/cmake/zstd/zstdConfig.cmake` 按 `${ANDROID_ABI}` 选择静态库。
- 若自己编 OpenSSL，必须把两个 `.so` 的 SONAME/NEEDED 从 `libcrypto.so.1.1`、`libssl.so.1.1` 改成 `libcrypto_1_1.so`、`libssl_1_1.so`（KDAB 预编译包已处理）；否则 APK 里的文件名与运行时依赖名不一致，加载 `libcooperation.so` 会失败。

**构建 APK：**

```bash
export ANDROID_HOME=/path/to/android-sdk
export ANDROID_NDK_HOME=$ANDROID_HOME/ndk/27.0.11902837
export ANDROID_ZSTD_DIR=$ANDROID_HOME/android-zstd

cd android
bash ./gradlew :app:assembleDebug
```

`android/modules/cooperation/build.gradle` 在检测到 `ANDROID_ZSTD_DIR` 时传入 `-Dzstd_DIR=${ANDROID_ZSTD_DIR}/lib/cmake/zstd`；未设置时退化为 deflate-only。

**结果：**

- 输出：`android/build/app/outputs/apk/debug/UOSAssistant_v1.0.2_debug.apk`（约 26 MB）。
- 包名 `com.deepin.assistant.debug`，`minSdk 26`、`targetSdk 34`，debug 签名校验通过；debug 构建只打包 `arm64-v8a`（app 的 abiFilter），模块同时编出了 `armeabi-v7a` 的 `libcooperation.so`。
- `libcooperation.so` 内含 `ZSTD_compressStream2`/`ZSTD_decompressStream` 定义，确认 zstd 静态链接；`SslCertConf` 与 JNI `initNative` 符号均在包内。
- 真机验证（2026-11-12）：OPPO PFJM10 / Android 13 / arm64-v8a，USB ADB（`ab3d0a36`）。`adb install` 成功，启动进入 `HomeActivity`，界面显示设备名与 IP（10.8.7.22），无 `UnsatisfiedLinkError`。
- 该 APK 为 `extractNativeLibs=false`，库直接从 APK 映射；进程 maps 中可见 `libcooperation.so`、`libcrypto_1_1.so`、`libssl_1_1.so` 对应的 APK 数据偏移（`0x68f000`/`0x16bc000`/`0xde9000`），确认静态 zstd、OpenSSL SONAME 补丁与依赖解析在真机上均正常。
- `initNative(ip, filesDir)` 在 `HomeActivity` 初始化时无条件调用，之后才更新自身 IP；真机已进入首页并显示 IP，说明 JNI 签名与调用路径正常。
- 证书是惰性生成的：`SecureConfig::clientContext()`/`serverContext()` 首次调用时才写 `<filesDir>/.cooperation/SSL/Barrier.pem`。Android 端 `sessionListen()` 仍是历史注释状态，客户端只能经扫码连接触发。
- 端到端扫码（2026-11-12）：在测试机 `10.8.15.62` 安装 1.2.8 PC 端并显示二维码，手机（`10.8.15.67`）扫码成功；手机侧 `files/.cooperation/SSL/Barrier.pem` 已生成（2664 字节，SHA-256 `3E:DE:99:CE:...:19:12`，与 PC 证书不同，确认是每设备证书），PC 侧 `10.8.15.62:51598` 与手机 `10.8.15.67:43404` 建立会话，VNC `10.8.15.62:51686` ↔ 手机 `5900` 连接并认证成功，投屏正常。

**`configs/crypt/cert.h` 断点修复：**

- Android `SecureConfig` 不再包含已删除的 `configs/crypt/cert.h`，改用 `SslCertConf` 生成/加载每设备自签名证书。
- Kotlin 调用改为 `initNative(ip, filesDir.absolutePath)`；JNI 将其设为 `<filesDir>/.cooperation`，证书持久化在 app 私有目录。
- `sslconf` 已加入 `libcooperation.so` 链接。
- `dev_share/cross-host/certificates/desktop.key` 与旧 `cert.h` 的硬编码私钥一致，但**没有**把旧证书重新写回代码；仍按安全修复后的方案生成每设备证书。

### 7.6 发布与回滚清单（2026-11-03）

**构建/打包：**

- Linux：`Build-Depends` 新增 `libzstd-dev`；zlib 仍由既有依赖链提供，不额外声明。运行时由 `${shlibs:Depends}` 带 `libzstd1`。
- Dev 包：`dde-cooperation-dev` 新增 `libzstd-dev`；zlib 依赖沿用既有工具链，不重复声明。
- Windows：vcpkg manifest 含 `zlib`/`zstd`，`clean_build.bat` 使用 `*-windows-static-md`；安装包不需要额外 DLL（尚未实机验证）。
- Android：debug APK 已带静态 zstd（`ANDROID_ZSTD_DIR` 指向两 ABI 的预编译包）；未提供时退化为 deflate。

**灰度/回滚开关：**

| 开关 | 作用 | 默认 |
|---|---|---|
| `DDE_COOPERATION_HTTPWEB_STREAM=0` | 客户端强制 legacy | 启用 |
| `DDE_COOPERATION_HTTPWEB_SERVER_STREAM=0` | 服务端 `stream/` 返回 404，且新 token 不写 `dtp` claim | 启用 |
| `DDE_COOPERATION_HTTPWEB_TOKEN_DTP=0` | 生成旧形状 token（无 `dtp` claim） | 写入 claim |
| `DDE_COOPERATION_HTTPWEB_STREAM_IDLE_TIMEOUT` | 空闲 watchdog 秒数 | 60 |
| `DDE_COOPERATION_HTTPWEB_CLIENT_NODELAY=0` | 关闭客户端 TCP_NODELAY（基准复现旧基线） | 开启 |

**回滚顺序：** 先设服务端 `SERVER_STREAM=0`（新连接全部 legacy，已发 token 的客户端遇 404 自动回退），再按需设客户端 `STREAM=0`；无需降级二进制。旧数据无需迁移。

**发布前验证：**

- `lib_logic_tests` 全绿（当前 343 项）。
- `build-core` Release 构建通过。
- 1k/10k/100k 基准逐文件校验一致；1M/10GB 大规模验收见调研报告 §6.3。
- 新旧 token、旧客户端行为、服务端关闭 stream 三条兼容路径均有集成测试。

## 8. 会话日志

### 2026-11-12 — 真实目录全树验证 + 根内符号链接保留

- 真实工程目录 `youqu-upstream`（同机 loopback）：1 189 文件/406 目录/81.58 MB，stream 2.295 s、legacy 7.597 s（约 3.3×）；stream 0 info + 1 download、最长停顿 44 ms，legacy 407 info + 1 178 download、908 ms。非空文件 SHA-256 全一致；stream 保留 11 个空文件和 10 个空目录，legacy 全部丢失。
- 发现 DTP2 跟随符号链接会读到所选根之外的文件。`StreamWalker` 增加根内 containment：越界/不可解析计入 `skippedOutside` 并上报；`WalkStats`/`StreamProducerStats` 暴露该计数。
- 整棵测试机 `~/work`（113 259 普通文件/13 641 目录/18.09 GB，1 819 个符号链接，其中 68 个越界或不可解析）跨机 stream：
  - 早期 114 776 文件、20.762 GB、327.1 s、0 失败；清单对照发现 99 个源路径缺失。根因是全局 `(dev,inode)` visited 把 `cpp-12 -> gcc-12-base` 这类兄弟别名当成环，真实路径被跳过。
  - 改为“当前路径祖先链”环检测后，115 093 文件、20.772 GB、321.6 s、0 失败；0 缺失/大小不符，1 834 条内部链接别名副本（额外 2.68 GB）。
  - 最终实现 Symlink 帧：新客户端带 `links=1`，根内链接保留并还原、根外跳过。整树 113 259 文件 + 1 751 链接、18.092 GB、314.6 s、0 失败、RSS 20.3 MB、最长停顿 5.343 s、0 info + 1 download；源/输出 0 缺失/0 多余/0 大小不符，1 751 个链接解析目标 0 不一致，68 个外链跳过，无 `.part`。重复的 2.68 GB 消失。
- 协议：新增 `Symlink` 帧（目标为相对链接父目录的规范化路径，可含 `..` 但不得越根）；服务端仅在新客户端请求 `links=1` 时发送，响应头 `X-DTP-Links`；旧客户端仍走“跟随+实体化”，旧服务端忽略参数也不影响新客户端。`DDE_COOPERATION_HTTPWEB_STREAM_LINKS=0` 可回退。
- 权限限制：帧携带 mode，但接收端未 `chmod`；真实目录落盘 mode 由 umask 决定（stream 664、legacy 644），只读/可执行位不保留。空文件/空目录只有 stream 保留。
- 测试：新增 Symlink 帧往返/非法目标、walker preserve/outside/dangling、producer/consumer 链接还原、sender `links=1` 开关和 FileClient 端到端集成；`StreamWalkerTest` 19 项，完整 `lib_logic_tests` 343 项全绿。
- 结论：真实目录传输可行；`~/work` 全树约 5 分 15 秒完成，根外读取已封堵，根内链接按原语义还原且不再复制目标内容。仍未做整棵 legacy 对照（预计 10 分钟以上）和两台真实 GUI 之间的传输。

### 2026-11-12 — Android 真机扫码投屏 + DTP2 流端到端验证

- 环境：OPPO PFJM10（Reno7 5G），Android 13，arm64-v8a；USB ADB（序列号 `ab3d0a36`）可正常使用。
- 安装：`adb install -r android/build/app/outputs/apk/debug/UOSAssistant_v1.0.2_debug.apk` 成功，包名 `com.deepin.assistant.debug`。
- 启动：`SplashActivity` → `HomeActivity`，界面显示 “UOS助手 / 扫码连接设备 / OPPO Reno7 5G | IP: 10.8.7.22”，无崩溃。
- 原生库：APK 为 `extractNativeLibs=false`；`/proc/<pid>/maps` 中出现 APK 数据偏移 `0x68f000`（`libcooperation.so`）、`0x16bc000`（`libcrypto_1_1.so`）、`0xde9000`（`libssl_1_1.so`）的映射，证明 `libcooperation.so`、静态 zstd 与打过 SONAME 补丁的 OpenSSL 均已在真机加载。
- JNI：`HomeActivity.initView()` 无条件调用 `initNative(ip, filesDir.absolutePath)`，之后界面才显示自身 IP；应用未出现 `UnsatisfiedLinkError`，说明 JNI 签名与配置目录传递正常。
- PC 端：测试机 `10.8.15.62`（UOS 25 x86_64）从源码重编并安装 1.2.8；运行中的 `dde-cooperation` 已加载 `/usr/lib/x86_64-linux-gnu/libcooperation-core.so.1.0.0`，库内含 `Stream disabled.`、`DDE_COOPERATION_HTTPWEB_SERVER_STREAM`、`ZSTD_compressStream2`/`ZSTD_decompressStream`，`NEEDED` 含 `libzstd.so.1`，确认不是旧库。
- 扫码：手机连到 `10.8.15.0/24`（`10.8.15.67`），扫描 PC 端“手机协同”二维码成功；手机侧 `files/.cooperation/SSL/Barrier.pem` 已生成（2664 字节，SHA-256 `3E:DE:99:CE:...:19:12`，与 PC 证书不同，确认是每设备证书）。
- 会话/投屏：PC `10.8.15.62:51598` ↔ 手机 `10.8.15.67:43404` 保持 ESTAB；PC `10.8.15.62:51686` ↔ 手机 `5900` 建立 VNC 连接，日志显示 “VNC authentication succeeded”，投屏正常。
- DTP2 流（同一构建的 loopback 基准）：默认 token（含 `dtp` claim）10k×4KB（40.96 MB）耗时 0.344 s、`http_info_requests=0`、`http_download_requests=1`、10000/10000 文件、CRC 校验 `verified=ok`、最长停顿 22 ms。旧形状 token（`DDE_COOPERATION_HTTPWEB_TOKEN_DTP=0`）自动回退 legacy，`http_info_requests=74`、`http_download_requests=10000`、同样校验通过（48.5 s），证明能力协商和回退都在真实构建上生效。
- 跨机 A/B（测试机 `10.8.15.62` 做服务端，开发机 `10.8.12.63` 做客户端；两端 `TCP_NODELAY` 开启，逐文件 CRC 校验）：10k×4KB legacy 50.73 s → stream 0.281/0.272 s（约 180×），CPU 3.62 → 0.36 s，最长停顿 312 → 6–7 ms，HTTP 请求 74 info + 10000 download → 0 info + 1 download；1 GB 文本单文件 legacy 9.24 s → stream 0.802 s（约 11.5×），CPU 6.99 → 0.91 s，均校验通过。
- 真实 ISO（`~/Downloads/uos-test.iso`，8 128 419 840 字节，测试机 HDD，传输前清 page cache）：legacy 75.40 s → stream 69.45 s（约 1.09×），CPU 53.83 → 52.43 s，RSS 11.8 → 16.2 MB，最长停顿 240 → 68 ms，HTTP 请求 1 info + 1 download → 0 info + 1 download；两端 SHA-256 均为 `79ec1cde…510a7b`。ISO 不可压缩，DTP2 无数量级收益但也无劣化，约 8% 差异应视为同量级。
- 基准工具新增远程模式：`--serve` 在一端托管目录并写 token 文件，`--connect HOST` 在另一端用 token 拉取；旧的单进程 loopback 模式保持不变。
- 权限：`adb shell pm grant` / `appops set` 仍被域管拒绝（shell 无 `GRANT_RUNTIME_PERMISSIONS`、`MANAGE_APP_OPS_MODES`）；CAMERA 已由用户手动授予。
- 结论：扫码、TLS、会话、投屏与 DTP2 流/回退均在真实设备与真实构建上验证通过；跨机 A/B 进一步确认小文件收益不是 loopback 假象。仍未覆盖：两台真实 GUI 应用之间的文件传输、真正的旧版本二进制矩阵、Windows 实机。

### 2026-11-03 — Phase 5：打包与大规格验收

- 打包：Debian `Build-Depends` 增加 `libzstd-dev`；Windows vcpkg manifest 增加 `zstd`，`clean_build.bat` 改用 `*-windows-static-md` 三元组，使 zlib/zstd 静态进入二进制、安装包不带额外 DLL；Android 随后补齐 arm64/armv7 的 zstd 预编译并成功出包（见下）。
- 验收：100k 复测 2.58 s；1M×4KB（4 GB，NVMe）58.5 s、CPU 33.1/71.5 s、RSS 56.5 MB、最长停顿 42 ms；10 GB 单文件 42.3 s、RSS 20.0 MB、最长停顿 765 ms；两份数据均逐文件校验一致。
- 复现：基准新增 `DDE_COOPERATION_HTTPWEB_CLIENT_NODELAY=0/1` 注入，可在 `--no-delay` 缺省时精确复现“两端 Nagle 全开”的旧基线。
- 能力协商：`genToken` 新增 `dtp` claim（v2 + codec 列表），`getDtpFromToken()` 解析；无 claim 的旧 token 直接走 legacy，不再先发一次失败的 stream 请求；`DDE_COOPERATION_HTTPWEB_TOKEN_DTP=0` 可生成旧形状 token 做回滚/兼容演练。
- 兼容测试：新增 `LegacyTokenUsesLegacyProtocol`、`StreamDisabledByEnvUsesLegacyProtocol`、`ServerStreamDisabledFallsBackToLegacy`，分别模拟旧 token、旧客户端、服务端关闭 stream，均断言走 info/download 端点且文件正确落盘。
- 服务端新增 `DDE_COOPERATION_HTTPWEB_SERVER_STREAM=0` 灰度开关：`stream/` 返回 404，客户端自动回退 legacy；开关在启动时生效时，`genToken` 也不再写 `dtp` claim，客户端直接走 legacy、不产生一次 404。
- Android：补齐 arm64/armv7 的 OpenSSL 1.1.1w 与 zstd 1.5.5 预编译；把 `configs/crypt/cert.h` 断点迁移到 `SslCertConf`（JNI `initNative(ip, filesDir)`，证书存 app 私有目录）；`libcooperation.so` 与 debug APK 构建成功，APK 约 26 MB、`ZSTD_*` 静态链接。
- 未完成：Windows 实机构建、Android APK 真机/模拟器安装验证、真正的旧版本二进制兼容矩阵、故障注入下的自动回退演练。
- 测试：`lib_logic_tests` **330/330** 通过；`build-core` 生产构建通过；Android `assembleDebug` 构建通过；stream 基准仍为 1 次 download、0 次 info。

### 2026-11-03 — Phase 4：断点续传、watchdog、故障注入

- 目标：把“能传”推进到“断网/挂起可恢复、停止不僵死”。
- 完成：
  - `StreamProducerOptions.skipEntries`：按确定性遍历跳过已确认帧；`from` 超过枚举总数时报错，避免“只发 END 假成功”。
  - 服务端 `from=N` 正式支持（`off>0` 仍 501）；`serveStream` 把 from 传给生产者。
  - `StreamConsumer`：新增 `resumeRoot` 续传模式、`completedEntries()`、每帧 checkpoint 回调；续传时禁止 RootDir/RootFile，已有最终文件视为冲突。
  - `FileClient`：最多 3 次尝试、200/800ms 退避；checkpoint 用 token+name 哈希定位、临时文件+原子 rename、1s 节流写入、失败立即写、成功后删除；损坏/丢失 checkpoint 自动从 0 开始；续传冲突换新根重来。
  - 空闲 watchdog：默认 60s 无数据即断开重试，环境变量可调。
  - 故障注入：TCP 代理“第一次连接中途切断”和“挂起不转发”两条用例，均自动续传并逐字节校验通过。
- 测试：`lib_logic_tests` **322/322** 通过；新增 `StreamFaultTest` 2 项、`StreamConsumerTest` 续传 2 项、`StreamSenderTest.FromSkipsCompletedFrames` 1 项。
- 回归：Phase 4 完成后重跑 stream 基准，10k 0.276 s、100k 2.58 s，与 Phase 3 记录（0.275/2.50 s）差异在噪声内；1k 0.067–0.072 s。为精确复现“两端 Nagle 全开”旧基线，基准新增 `DDE_COOPERATION_HTTPWEB_CLIENT_NODELAY=0` 注入（生产默认 1）。
- 已知限制：
  - checkpoint 按 1s 节流，进程崩溃最多回退约 1s 的文件；续传冲突会自动换新根重来，不覆盖旧树。
  - 源树在两次尝试间变化未做 size/mtime 校验（v1 整文件粒度）；索引可能错位，冲突时整树重传。
  - 慢盘、磁盘满、用户停止的专项故障用例待补。
- 下一步：Phase 5 打包/兼容矩阵（Linux 共享 zstd 依赖、Windows/Android 静态），并补齐 P4-5。

### 2026-11-03 — D3 定稿 + TCP_NODELAY 生产开启

- 用户确认：D3 采用“Linux 共享 zstd、Windows/Android 静态”；`TCP_NODELAY` 生产默认开启。
- 代码：
  - `FileServer::start()` 增加 `SetupNoDelay(true)`；
  - `FileClient` 构造后对 `_httpClient` 调用 `SetupNoDelay(true)`；
  - 基准工具把显式覆盖移到 `start()` 之后，保证 `--legacy` 不带 `--no-delay` 仍能复现旧基线。
- 验证：
  - `lib_logic_tests` 317/317 通过；
  - 1k legacy 复现：nodelay=0 为 44.84 s、nodelay=1 为 4.47 s；
  - 生产构建 `build-core` 通过。
- 文档：三份设计文档同步 D3/D11，并在调研报告新增 §6.6 说明 Nagle 原理与取舍。
- 下一步：Phase 4（整文件 checkpoint/`from` 重连、故障注入）。

### 2026-11-02 — Phase 2 完成 + Phase 3 接收端接入

- 目标：完成发送端全部任务，并把接收端接到真实 HTTP 下载流程，为“改造前后”对照提供可运行路径。
- 完成：
  - `StreamProducer`：前导→遍历→帧→压缩→sink；每文件 flush；取消/文件缩短/不可读文件语义；事件回调；8 项测试。
  - `FileServer::serveStream`：`stream/<b64>&token&codec&level&from&off`；`SetOpenEndedBody()`（无 Content-Length）；`Connection: close`；from/off≠0 返回 501；生产者在 FileServer 持有的线程运行，`stop()` 可 join。
  - 背压：发送侧 2MB 高水位 + `SendResponseBodyAsync`，END 后等队列排空再关闭；接收侧 `SSLSession`/`SSLClient` 新增 `PauseReceive/ResumeReceive`，队列超 4MB 暂停读取、低于 1MB 恢复。
  - `StreamConsumer`：解压/解析/`.part` 落盘/CRC/rename；根目录按 web 名命名、冲突 `(1)`、拒绝穿越/绝对路径/跨 symlink；10 项测试。
  - `FileClient::downloadStream`：一次请求下载整棵选择；`WEB_FILE_*`/`onProgress` 映射；404 或非流响应在写盘前回退 legacy；`DDE_COOPERATION_HTTPWEB_STREAM=0` 关闭。
  - **修复 `HTTPRequest::url()` 携带 ` HTTP/1.1` 导致最后一个 query 参数被污染**（`codec=zstd HTTP/1.1`）；GET 入口统一截断到首个空格，legacy `offset` 同样受益。
- 改动文件：
  - 新增：`streamproducer.{h,cpp}`、`streamconsumer.{h,cpp}`、`tests/logic/httpweb_stream_producer_test.cpp`、`tests/logic/httpweb_stream_consumer_test.cpp`、`tests/logic/httpweb_stream_sender_test.cpp`
  - 修改：`fileserver.{h,cpp}`、`fileclient.{h,cpp}`、`streamproducer.h`、`http_response.{h,cpp}`、`ssl_session.{h,cpp}`、`ssl_client.{h,cpp}`、`httpweb_fork_test.cpp`、`httpweb_stub_test.cpp`
- 测试命令/结果：
  - `lib_logic_tests` → **317/317 通过**
  - `StreamProducerTest.*` 8/8；`StreamConsumerTest.*` 10/10；`StreamSenderTest.*` 5/5
  - `HttpWebIntegrationTest.*` 连跑 5 次稳定；断言下载文件落在 `cliDir/test.txt`，确认走的是 stream 而不是 fallback
- 风险/偏差：
  - 接收端整树把根目录命名为“请求的 web 名最后一段”，与 legacy 保存路径语义一致；服务端磁盘名不同时不再影响落点。
  - 生产端 `IsConnected()` 仍是跨线程普通读（与既有代码一致），发送背压计数由 `onSent` 维护，未跨线程读 `bytes_pending()`。
  - D3（zstd 链接策略）与 `TCP_NODELAY` 生产开关仍待用户确认。
  - 100k legacy 基线仍在后台运行（已约 3.9 万文件）。
- A/B 实测（同机、Release、独立端口、树校验通过）：
  - 1k×4KB：legacy 44.89 s / stream 0.057 s（787×）；legacy+nodelay 4.65 s。
  - 10k×4KB：legacy 447.98 s / legacy+nodelay 45.59 s / stream 0.275 s（1629× / 166×）；CPU 18.6 s → 0.52 s。
  - 100k×4KB：legacy 4 484 s（CPU 178.7 s，RSS 22.8 MB，最长停顿 12.2 s）→ stream 2.50 s（CPU 5.16 s，RSS 21.4 MB，最长停顿 27 ms），1793×；100000/100000 文件校验一致。
  - 数据与解读已写入调研报告 §6.3（含 nodelay 收益拆分、RSS 代价、请求数对比）。
- 下一步：Phase 4 断点/故障注入。

### 2026-11-02 — P2-1 遍历器 + 官方 GTest 接入 + zstd 链接修复

- 目标：安装 GTest 后把流测试接进正式门禁；实现 P2-1 确定性遍历；为“改造前后对照”搭好报告框架。
- 完成：
  - 官方 `-DDOTEST=ON` 配置/构建通过，`lib_logic_tests` 运行 `HttpWebStream*`/`HttpWebHttpResponse*` 共 47 项全绿。
  - 修复 `httpweb_stub_test.cpp` 中跟随 `requestInfo`/`walkFolderEntry` 新签名的调用。
  - 修复官方构建下的两处 CRC 测试魔数偏移（改用精确偏移）。
  - **修复 zstd 静态链接的符号插入崩溃**：Linux 改为优先共享库；详见 7.3。
  - **修复 walkDownload 失败路径不发终态导致 UI 挂起**（官方集成测试抓到）：新增 `InfoStatus` 区分 404 与网络/协议错误；网络错误发 `WEB_DISCONNECTED` 并中止，404 发 `WEB_NOT_FOUND` 后继续；下载超时也补发 `WEB_DISCONNECTED`；`walkFolder`/`walkFolderEntry` 改为返回 bool 向上传播。
  - 新增 `streamwalker.{h,cpp}`：确定性遍历，13 项测试全绿（排序、空目录、根文件、环、特殊文件、深度上限、不安全名、Unicode、可中断、跨次稳定）。
  - 调研报告新增 §6 对照实验设计（含 Nagle 混淆项的处理）、§6.2 实测基线、§6.3 改造后填写模板、§6.5 优缺点对照。
- 改动文件：
  - 新增：`src/lib/common/httpweb/streamwalker.{h,cpp}`、`tests/logic/httpweb_stream_walker_test.cpp`
  - 修改：`src/lib/common/httpweb/CMakeLists.txt`（zstd 链接策略）、`tests/logic/httpweb_stub_test.cpp`、`tests/logic/httpweb_stream_frame_test.cpp`
  - 文档：本文件、`httpweb-compressed-stream-research.md` §6
- 测试命令/结果：
  - `cmake -S . -B build-dev -DCMAKE_BUILD_TYPE=Debug -DDOTEST=ON` → 成功（GTest 1.12.1）
  - `cmake --build build-dev --target lib_logic_tests` → 成功
  - `lib_logic_tests --gtest_filter='HttpWebStream*:HttpWebHttpResponse*'` → 47/47 通过
  - `lib_logic_tests --gtest_filter='StreamWalkerTest.*'` → 13/13 通过
  - 100k legacy 基线仍在后台运行（约 50 分钟后完成）
- 阻塞/风险：
  - D3 的“优先静态”与 Linux 实测冲突，等用户确认（7.3）。
  - P2-2 生产者尚未开始；对照实验缺“改造后”数据。
- 下一步：`P2-2` StreamProducer。
- git status：见会话结束时 `git status --short`（未提交）。

### 2026-11-02 — P0-1 基准工具与 P0-7 legacy 基线

- 目标：实现 P0-1 基准工具与数据集生成器，跑通 P0-7 legacy 基线，关闭 Phase 0。
- 完成：
  - 新增 `tests/benchmarks/`：数据集生成（文件数/大小/目录数/深度/内容模式/单大文件）+ 进程内 legacy 跑分（独立 server/client IO 线程、loopback、超时、重复、逐文件 CRC 校验）。
  - 指标：墙钟、CPU user/sys、RSS 峰值、最长无数据停顿、每文件传输/周期、HTTP 请求数与连接数。
  - `FileClient` 新增只读计数器 `requestCounters()`（HEAD/info/download/连接数），不改变任何现有签名。
  - 修复基准发现的真实缺陷：小文件整包到达时只走 `RES_FINISH`，该分支写入最后一块却没有计入进度，导致 UI 进度漏计小文件。现在 `RES_FINISH` 也会 `onProgress`。
  - 基线：1k/10k 实测，100k 运行中；得到“每文件约 40 ms 来自 Nagle + delayed ACK”的结论（见 7.2）。
- 改动文件：
  - 新增：`tests/benchmarks/CMakeLists.txt`、`tests/benchmarks/httpweb_transfer_benchmark.cpp`
  - 修改：`CMakeLists.txt`（`BUILD_HTTPWEB_BENCH` 选项）、`src/lib/common/httpweb/fileclient.{h,cpp}`（计数器 + 进度修复）
  - 文档：本文件 §2/§7/§7.2/§8
- 测试命令/结果：
  - `cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DBUILD_HTTPWEB_BENCH=ON` → 成功
  - `cmake --build build-bench --target httpweb_transfer_benchmark` → 成功
  - `--generate` 1000/10000/100000 × 4KB：0.009 s / 0.069 s / 0.675 s，生成 4 MB / 40 MB / 391 MB
  - `--legacy` 1k：44.8 s，1000/1000 文件 CRC 校验通过；`--no-delay` 同数据 4.44 s
  - `--legacy` 10k：448 s，10000/10000 文件 CRC 校验通过，CPU 18.2 s，RSS 14 MB
  - `--legacy` 100k：后台运行中（预计约 75 分钟），结果补记到本节
  - 全项目 `build-core` 构建仍通过（`DOTEST=OFF`，本机无 GTest）
- 阻塞/风险：
  - 100k 基线尚未跑完；若中断，以 1k/10k 的线性模型为准并注明。
  - 是否默认开启 `TCP_NODELAY` 待用户决策（见 7.2），开启后必须重记基线。
- 下一步：`P2-1` 确定性遍历（StreamWalker）。
- git status：见会话结束时 `git status --short`（未提交）。

### 2026-10-29 — Phase 1 编解码/帧 + Phase 0 稳定性修复

- 目标：实施 Phase 1（P1-1..P1-5），并完成 Phase 0 可独立落地的稳定性修复。
- 完成：
  - 新增 `streamcodec`（none/deflate/zstd 流式编解码，分块/flush/finish/输出上限）与 `streamframe`（前导、帧编码、增量解析、CRC、END、路径校验）。
  - 新增 47 项测试：`httpweb_stream_codec_test.cpp`（15）、`httpweb_stream_frame_test.cpp`（26）、`httpweb_http_response_test.cpp`（6）。
  - CMake：httpweb 强制 zlib；zstd 可选（`HTTPWEB_HAVE_ZSTD`），优先静态链接。
  - P0-2：`TransferWorker::_canceled` 改 atomic、`_status` 加锁快照；`WebInterface` 回调加锁访问；`HTTPFileClient` handler 互斥快照、`_canceled` 每请求重置。
  - P0-3（部分）：下载超时后主动断开并等待连接结束，避免旧响应污染下一次请求。
  - P0-4：`offset > size` 返回 416、`stoll` 异常捕获、`webBind` 失败中止传输、`requestInfo` 返回 bool、`downloadFile` 返回真实结果、非普通文件 404。
  - P0-5：`HTTPResponse::ConsumeHeader()` 保留同包 body；`X-DTP-Stream` 检测；流模式禁用 CRLFCRLF 启发式；`_header_parsed` 显式状态位。
  - P0-6：`FileSizeCounter` 使用 `(dev,inode)` visited 集合 + 迭代遍历；客户端目录递归深度上限 64。
- 改动文件：
  - 新增：`src/lib/common/httpweb/streamcodec.{h,cpp}`、`streamframe.{h,cpp}`、`tests/logic/httpweb_stream_codec_test.cpp`、`httpweb_stream_frame_test.cpp`、`httpweb_http_response_test.cpp`
  - 修改：`src/lib/common/httpweb/CMakeLists.txt`、`fileclient.{h,cpp}`、`fileserver.cpp`、`syncstatus.h`、`src/lib/common/manager/transferworker.{h,cpp}`、`filesizecounter.{h,cpp}`、`src/infrastructure/netutil/include/http/http_response.h`、`src/infrastructure/netutil/src/http/http_response.cpp`、`src/infrastructure/netutil/src/http/https_client.cpp`
- 测试命令/结果：
  - `cmake -S . -B build-core -DCMAKE_BUILD_TYPE=Debug && cmake --build build-core -j$(nproc)` → 成功（DOTEST=OFF，本机无 GTest）。
  - 本机无 GTest/无网络/无 root，用最小 gtest 兼容 shim 编译并运行真实测试源码：
    `g++ -std=c++17 -I<shim> -Isrc/lib/common/httpweb -DHTTPWEB_HAVE_ZSTD=1 tests/logic/httpweb_stream_codec_test.cpp tests/logic/httpweb_stream_frame_test.cpp src/lib/common/httpweb/streamcodec.cpp src/lib/common/httpweb/streamframe.cpp -lz -lzstd`
    → 41/41 通过；ASan+UBSan 下再跑一遍 41/41 通过，无泄漏。
  - `httpweb_http_response_test.cpp` 链接 `build-core/.../libnetutil.a`、`libbasekit.a`、`libfmt.a`（`-fno-access-control`）→ 6/6 通过。
  - 有 GTest 的环境应执行：`-DDOTEST=ON` 后 `lib_logic_tests --gtest_filter='HttpWebStream*:HttpWebHttpResponse*'`（本机未执行）。
- 阻塞/风险：
  - 本机缺 GTest，DOTEST 目标无法配置；shim 只验证测试逻辑，不等于 CI 结果。
  - P0-1 基准、P0-7 基线未做，Phase 0 未关闭。
  - `zstd::libzstd_static` 已确认可链接进共享库；Windows/Android 打包仍待 P5-1。
- 下一步：P0-1（基准工具与数据集生成器），然后 P0-7；之后进入 Phase 2（P2-1 确定性遍历）。
- git status：见会话结束时的 `git status --short`（未提交）。

> 后续 Phase 的任务级状态在实施计划第 4 节；本文只维护阶段状态和当前 Phase 的任务状态，避免两份表漂移。

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
10. **符号链接可能成环、越界，或把内容重复传输**：新客户端协商 `links=1` 时发 Symlink 帧保留根内链接，根外/不可解析跳过；旧客户端保持跟随+实体化。旧跟随模式的环检测用当前路径祖先链，不要用全局 `(dev,inode)` 集合（会把兄弟别名误判为环并丢路径）。
11. **不要依赖 `directory_iterator` 顺序做断点**：每层排序。
12. **不要直接写最终文件名**：`.part` + CRC/大小校验 + rename。
13. **失败不要发 `WEB_TRANS_FINISH`**：发 `WEB_IO_ERROR`/`WEB_DISCONNECTED`。
14. **不要在 stream 失败后混用 legacy**：已写文件后只 retry/resume，否则产生重复目录。

### 9.2 环境/测试坑

- **本机为 Qt6**（`/usr/lib/qt6`），不是 Qt5；GTest 1.12.1 已安装，`-DDOTEST=ON` 可直接配置。若换到无 GTest/无网络环境，才需要兼容 shim 或换机器。
- 逻辑测试二进制在 `build-dev/tests/logic/lib_logic_tests`；用 `cmake --build build-dev --target lib_logic_tests` 单独构建，避免被 daemon 的 gcov 增量链接问题阻塞。
- 项目根 `CMakeLists.txt` 默认 `DOTEST=OFF`；有 GTest 的环境才显式 `-DDOTEST=ON`。
- `tests/logic` 使用 GTest，`file(GLOB *_test.cpp)` 自动收录新增测试；新增文件无需改 CMake。
- `lib_logic_tests` 链接 `httpweb`，而 `httpweb` 现在 PUBLIC 链接 `ZLIB::ZLIB` 和 zstd，测试目标会自动获得。
- netutil 的 Catch2 通过 Gitee FetchContent 获取，网络不可用时不要阻塞；优先 `netutil_http_tests`。
- 已知部分测试会挂起/失败（见 `test-prj-running.sh` 注释）；不要把它们当成新回归。
- 真实目录远程验收不要只看 `result=ok`/CRC：客户端 `--src` 为空时 `verified=skipped`，必须在两端生成“路径+大小”清单做集合对照，并抽大文件 SHA-256；否则会漏掉“路径被别名吞掉但内容仍在”的问题。

### 9.3 禁令

- 不修改 `FileServer`/`FileClient` 公开方法签名；不删除 legacy 端点。
- 不把 UI 线程/业务层拖进 stream 细节。
- 不在 IO 线程读盘、写盘、压缩、解压、sleep。
- 不提交 `build*/`、生成文件、测试数据集。
- 不记录 token/密钥/用户文件内容到日志。
- 不未经用户确认改变 D1–D12。

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
| 符号链接：跟随还是保留 | 已实现：`links=1` 时保留根内链接并还原，根外跳过；旧客户端跟随+实体化 | 已决定 |
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
