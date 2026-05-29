# CI/CD 构建与发布指南

## 概览

项目使用 GitHub Actions 自动构建和发布。配置文件在 `.github/workflows/` 目录下。

## 文件结构

```
.github/workflows/
├── build.yml      # push main / PR 触发，验证三平台编译
└── release.yml    # push tag v* 触发，三平台构建 + 创建 GitHub Release
```

## 自动构建（每次 push main）

每次 push 代码到 main 分支或提交 PR，自动触发三平台编译验证：

| 平台 | Runner | 说明 |
|------|--------|------|
| Windows x64 | windows-latest | vcpkg 安装依赖，VS2022 编译 |
| Linux x86_64 | ubuntu-24.04 | apt 安装依赖，Ninja 编译 |
| macOS arm64 | macos-14 | Homebrew 安装 Ninja，最低支持 macOS 10.15 |

CI 中禁用了以下可选功能（CI 环境无法提供）：
- `USE_CUDA=OFF` — 无 GPU runner
- `USE_LOCAL_LLM=OFF` — llama.cpp 源码不在仓库中
- `USE_FAISS=OFF` — 无 FAISS 库
- `USE_ONNX=OFF` — 无 ONNX Runtime

## 发布新版本

### 步骤

```bash
# 1. 确保 main 分支 CI 全绿
git push origin main

# 2. 打版本 tag
git tag v0.3.0

# 3. 推送 tag（需要代理时加 -c 参数）
git -c http.proxy=http://127.0.0.1:7897 -c https.proxy=http://127.0.0.1:7897 push origin v0.3.0

# 不需要代理时
git push origin v0.3.0
```

### 自动发生的事

推送 `v*` tag 后，`release.yml` 自动：

1. 三台云服务器并行构建：

| 平台 | 产物 | 说明 |
|------|------|------|
| Windows x64 | `CloseCrab-Windows-x64.zip` | 便携版 ZIP |
| Linux x86_64 | `CloseCrab-Linux-x86_64.tar.gz` | 解压即用 |
| macOS arm64 | `CloseCrab-macOS-arm64.pkg` | 双击安装到 /usr/local/bin |

2. 全部成功后，自动创建 GitHub Release 页面并附带下载文件

### 查看进度

```bash
# 查看 release workflow 状态
gh run list --workflow release.yml --limit 3

# 查看已发布的 releases
gh release list

# 查看具体 run 的失败日志
gh run view <run-id> --log-failed
```

## 版本号规则

语义化版本 `vX.Y.Z`：
- `v0.2.1` — bug 修复
- `v0.3.0` — 新功能
- `v1.0.0` — 正式稳定版
- `v0.3.0-beta` / `v0.3.0-alpha` — 预发布（自动标记为 prerelease）

## 平台支持

| 平台 | 最低版本 | 架构 |
|------|----------|------|
| Windows | Windows 10 | x64 |
| macOS | 10.15 (Catalina) | arm64 (Apple Silicon) |
| Linux | Ubuntu 20.04+ | x86_64 |

macOS 最低 10.15 是因为项目大量使用 `std::filesystem`（该 API 在 10.15 才引入）。

## 常见操作

### 删除错误的 tag 重新发布

```bash
git tag -d v0.3.0
git push origin :refs/tags/v0.3.0
# 修复后重新打
git tag v0.3.0
git push origin v0.3.0
```

### Windows 构建慢

Windows 的 vcpkg 需要从源码编译 curl/zlib/openssl，首次约 15-20 分钟。后续有缓存会快一些。

### 本地构建（Windows，带完整功能）

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=G:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DUSE_CUDA=ON -DUSE_LOCAL_LLM=ON -DUSE_ONNX=ON -DUSE_FAISS=ON
cmake --build build --config Release
```

## 依赖管理

- **Windows**: vcpkg（`vcpkg.json` 声明 curl, zlib, openssl）
- **Linux**: apt-get（libcurl, zlib, openssl）
- **macOS**: 系统自带（curl, zlib 已预装）
- **跨平台 FetchContent**: spdlog, nlohmann_json, yaml-cpp, CLI11, sqlite3, httplib, capstone, ixwebsocket
