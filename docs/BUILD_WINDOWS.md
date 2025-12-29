# Windows WiX 构建方案（模板化方式）

## 📋 方案概述

由于项目包含两个独立应用（dde-cooperation 和 data-transfer），而CPack只能为一个CMake项目生成一个安装包，因此采用**基于模板的WiX打包方案**。

### 设计原则

1. **模板化方式**: 使用 `main.wxs.in` 模板文件，类似 Inno Setup 的 `.iss.in` 方式
2. **完全独立**: 两个应用各自生成独立的MSI安装包
3. **零重构**: 不需要修改现有的CMake项目结构
4. **翻译支持**: 每个应用独立的中英文翻译文件

## 🚀 快速开始

### 前置要求

1. **Visual Studio 2019/2022** (已安装C++构建工具)
2. **Qt 5.15.2** (MSVC版本)
   - Qt 5.15.2 MSVC2019 64-bit (用于x64构建)
   - Qt 5.15.2 MSVC2019 32-bit (用于x86构建，支持Win7)
3. **OpenSSL 3.x** (对应架构版本)
4. **WiX Toolset 6+** (必需)
   ```powershell
   winget install WiX.Toolset
   ```

### 构建流程

#### 1. 编译项目

```batch
# 配置和编译
mkdir build
cd build
cmake -G "Visual Studio 17 2022" -A x64 ^
    -D CMAKE_BUILD_TYPE=Release ^
    -D CMAKE_PREFIX_PATH="D:\Qt\5.15.2\msvc2019_64" ^
    -D QT_VERSION=5.15.2 ^
    -D APP_VERSION=1.1.23 ..
cmake --build . --config Release
```

**或者使用之前的构建脚本**:
```batch
clean_build.bat 1.1.23
```

编译完成后，output目录结构：
```
build/
└── output/
    └── Release/
        ├── dde-cooperation/
        │   ├── dde-cooperation.exe
        │   ├── Qt5*.dll
        │   ├── libcrypto-3-x64.dll
        │   ├── libssl-3-x64.dll
        │   └── translations/
        └── data-transfer/
            ├── deepin-data-transfer.exe
            ├── Qt5*.dll
            ├── libcrypto-3-x64.dll
            ├── libssl-3-x64.dll
            └── translations/
```

#### 2. 打包MSI安装包

```batch
# 打包64位版本
wix_build.bat 1.1.23 x64

# 打包32位版本（支持Win7）
wix_build.bat 1.1.23 x86
```

**输出**:
```
build/
└── _CPack_Packages/
    ├── dde-cooperation-1.1.23-win-x64.msi
    └── deepin-datatransfer-1.1.23-win-x64.msi
```

## 📦 模板化打包实现

### 目录结构

```
dist/wix/
├── main.wxs.in                          # WiX 主模板文件
└── scripts/lang/                        # 翻译文件
    ├── english.wxl                      # 英文通用翻译
    ├── chinese.wxl                      # 中文通用翻译
    ├── dde-cooperation-en.wxl           # dde-cooperation 英文翻译
    ├── dde-cooperation-chs.wxl          # dde-cooperation 中文翻译
    ├── data-transfer-en.wxl             # data-transfer 英文翻译
    └── data-transfer-chs.wxl            # data-transfer 中文翻译
```

### CMake 模板配置

每个应用的 CMakeLists.txt 中配置模板变量：

```cmake
# dde-cooperation
set(WIX_PROJ_NAME "dde-cooperation")
set(WIX_PRODUCT_NAME "Deepin Cooperation")
set(WIX_UPGRADE_CODE "A3AE65FC-2431-49AE-9A9C-87D3DBC2B7A4")
set(WIX_INSTALL_FOLDER "dde-cooperation")
set(WIX_EXE_NAME "dde-cooperation.exe")
configure_file(${CMAKE_SOURCE_DIR}/dist/wix/main.wxs.in 
               ${CMAKE_BINARY_DIR}/output/${PROJ_NAME}.wxs @ONLY)

# data-transfer
set(WIX_PROJ_NAME "data-transfer")
set(WIX_PRODUCT_NAME "Deepin Data Transfer")
set(WIX_UPGRADE_CODE "636B356F-47E1-491D-B66E-B254233FFCB1")
set(WIX_INSTALL_FOLDER "deepin-datatransfer")
set(WIX_EXE_NAME "deepin-data-transfer.exe")
configure_file(${CMAKE_SOURCE_DIR}/dist/wix/main.wxs.in 
               ${CMAKE_BINARY_DIR}/output/${PROJ_NAME}.wxs @ONLY)
```

### 打包流程

```
1. 运行 CMake 配置（生成 .wxs 模板文件到 build/output/）
   - build/output/dde-cooperation.wxs
   - build/output/data-transfer.wxs

2. 编译项目
   - build/output/Release/dde-cooperation/
   - build/output/Release/data-transfer/

3. 打包 MSI（wix_build.bat）
   a. 检查模板文件和编译输出是否存在
   b. 复制编译输出到临时目录
   c. 处理模板变量（版本号、架构等）
   d. 应用特定翻译文件
   e. 调用 wix build 生成 MSI
   f. 输出到 build/_CPack_Packages/
```

### 与 Inno Setup 的对比

| 特性 | Inno Setup | WiX (模板方式) |
|------|------------|----------------|
| 模板文件 | `setup.iss.in` | `main.wxs.in` |
| 翻译文件 | `scripts/lang/*.iss` | `scripts/lang/*.wxl` |
| CMake 集成 | `configure_file()` | `configure_file()` |
| 输出格式 | `.exe` | `.msi` |
| 安装包位置 | `build/installer-inno/` | `build/_CPack_Packages/` |
| 参数传入 | `#define VAR` | `@VAR@` |

## 🔧 完整构建示例

### 64位版本

```batch
REM 1. 编译
mkdir build
cd build
cmake -G "Visual Studio 17 2022" -A x64 -D CMAKE_BUILD_TYPE=Release -D CMAKE_PREFIX_PATH="D:\Qt\5.15.2\msvc2019_64" -D QT_VERSION=5.15.2 -D APP_VERSION=1.1.23 ..
cmake --build . --config Release
cd ..

REM 2. 打包
wix_build.bat 1.1.23 x64
```

### 32位版本（支持Win7）

```batch
REM 1. 编译
mkdir build
cd build
cmake -G "Visual Studio 17 2022" -A Win32 -D CMAKE_BUILD_TYPE=Release -D CMAKE_PREFIX_PATH="D:\Qt\5.15.2\msvc2019_86" -D QT_VERSION=5.15.2 -D APP_VERSION=1.1.23 ..
cmake --build . --config Release
cd ..

REM 2. 打包
wix_build.bat 1.1.23 x86
```

## 🆚 与Inno Setup对比

| 特性 | Inno Setup | WiX MSI |
|------|------------|---------|
| 输出格式 | EXE | MSI |
| Windows 7支持 | ❌ | ✅ (x86版本) |
| 系统集成 | 一般 | 好（原生） |
| 升级机制 | 自定义 | 原生Windows Installer |
| 构建方式 | ISCC.exe | wix.exe |
| 文件收集 | 脚本指定 | 递归包含output目录 |
| 代码复杂度 | 高（.iss脚本） | 低（动态生成） |

## ⚠️ 故障排查

### 问题1: WiX Toolset未找到

```
Error: WiX Toolset 6+ not found in PATH
```

**解决**:
```powershell
winget install WiX.Toolset
```

### 问题2: 编译输出不存在

```
Error: dde-cooperation build output not found
```

**解决**: 先运行CMake编译项目
```batch
cd build
cmake --build . --config Release
cd ..
wix_build.bat 1.1.23 x64
```

### 问题3: WiX build失败

```
error WIX0034: The Product/@UpgradeCode attribute's value is not a GUID
```

**解决**: 检查WiX脚本中的GUID格式，确保符合标准

### 问题4: MSI安装时缺少文件

**原因**: WiX的`<Files Source="*" Recursive="yes" />`可能漏掉某些文件

**解决**: 检查output目录，确保所有必需文件都存在

## 📝 架构说明

### 为什么不使用CPack

**CPack的限制**:
- 一个CMake项目只能生成一个package target
- 无法为多个可执行文件生成独立的MSI
- 需要大幅重构项目结构

**当前方案的优势**:
- ✅ 零CMake重构
- ✅ 直接使用已编译的output目录
- ✅ 每个应用独立打包
- ✅ 简单易懂，易于维护

### 文件结构

```
项目根目录/
├── wix_build.bat          # 主打包脚本
├── build/                 # CMake构建目录
│   ├── output/Release/    # 编译输出
│   │   ├── dde-cooperation/
│   │   └── data-transfer/
│   └── _CPack_Packages/   # MSI输出
│       ├── dde-cooperation-*.msi
│       └── deepin-datatransfer-*.msi
└── docs/
    └── BUILD_WINDOWS.md   # 本文档
```

## 🚀 CI/CD集成

### GitHub Actions示例

```yaml
name: Build Windows MSI

on:
  push:
    tags:
      - 'v*'

jobs:
  build:
    runs-on: windows-latest
    strategy:
      matrix:
        arch: [x64, x86]
    
    steps:
      - uses: actions/checkout@v3
      
      - name: Install Qt
        uses: jurplel/install-qt-action@v3
        with:
          version: '5.15.2'
          arch: ${{ matrix.arch == 'x64' && 'win64_msvc2019_64' || 'win32_msvc2019' }}
      
      - name: Install WiX
        run: winget install WiX.Toolset
      
      - name: Build with CMake
        run: |
          mkdir build
          cd build
          cmake -G "Visual Studio 17 2022" -A ${{ matrix.arch }} `
            -D CMAKE_BUILD_TYPE=Release `
            -D CMAKE_PREFIX_PATH="$env:Qt5_DIR" `
            -D QT_VERSION=5.15.2 `
            -D APP_VERSION=${{ github.ref_name }} ..
          cmake --build . --config Release
      
      - name: Package with WiX
        run: wix_build.bat ${{ github.ref_name }} ${{ matrix.arch }}
      
      - name: Upload MSI
        uses: actions/upload-artifact@v3
        with:
          name: msi-${{ matrix.arch }}
          path: build/_CPack_Packages/*.msi
```

## 📚 参考资源

- [WiX Toolset 6+ 文档](https://wixtoolset.org/docs/)
- [WiX 命令行工具](https://wixtoolset.org/docs/tools/wix/)
- [项目GitHub](https://github.com/linuxdeepin/dde-cooperation)

## ✨ 总结

这个方案采用**模板化方式**实现 WiX 打包，类似 Inno Setup 的实现方式，避免了 CPack 的多应用限制：

1. **模板化方式** - 使用 `main.wxs.in` 模板文件
2. **参数化配置** - 通过 CMake 变量传入应用参数
3. **完全独立** - 每个应用一个 MSI
4. **零重构** - 不修改 CMake 结构
5. **翻译支持** - 每个应用独立的中英文翻译
6. **简单直接** - 一个脚本完成打包

### 完整的工作流程

```batch
# 1. CMake 配置（生成模板文件）
cmake -B build -S . -G "Visual Studio 17 2022" -A x64

# 2. 编译项目
cmake --build build --config Release

# 3. 打包 MSI
wix_build.bat 1.1.23 x64
```

### 生成的文件

- **CMake 配置阶段**: `build/output/dde-cooperation.wxs`, `build/output/data-transfer.wxs`
- **打包阶段**: `build/_CPack_Packages/dde-cooperation-1.1.23-win-x64.msi`

所有代码已准备好，可以在 Windows 环境中测试！
