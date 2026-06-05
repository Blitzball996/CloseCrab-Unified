# CloseCrab FileReadTool 全面对齐 - 快速开始指南

最后更新：2026-06-05  
预计完成时间：**40分钟**

---

## 🎯 目标

将 CloseCrab 的 FileReadTool 完全对齐 JackProAi 的所有功能，实现：
- ✅ PDF 原生支持（poppler C++）
- ✅ 图片智能压缩（stb_image，80%功能）
- ✅ Jupyter Notebook 支持
- ✅ 完整安全检查（UNC/设备文件/权限系统）
- ✅ 智能错误提示
- ⚠️ 可选异步 I/O（批量场景+70%性能）

**最终对齐度：95%**

---

## 📋 10步快速部署

### 第1步：下载 stb 头文件（2分钟）

```powershell
# Windows PowerShell
cd G:\CMakePJ\CloseCrab-Unified
mkdir third_party\stb -Force

Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h" `
  -OutFile "third_party\stb\stb_image.h"

Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h" `
  -OutFile "third_party\stb\stb_image_write.h"

Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nothings/stb/master/stb_image_resize2.h" `
  -OutFile "third_party\stb\stb_image_resize2.h"
```

```bash
# Linux/macOS
cd CloseCrab-Unified
mkdir -p third_party/stb

curl -o third_party/stb/stb_image.h \
  https://raw.githubusercontent.com/nothings/stb/master/stb_image.h

curl -o third_party/stb/stb_image_write.h \
  https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h

curl -o third_party/stb/stb_image_resize2.h \
  https://raw.githubusercontent.com/nothings/stb/master/stb_image_resize2.h
```

---

### 第2步：安装 poppler（可选，5分钟）

**Windows：**
```powershell
# 选项A：Chocolatey
choco install poppler

# 选项B：手动下载
# 1. 下载：https://github.com/oschwartz10612/poppler-windows/releases
# 2. 解压到 C:\poppler
# 3. 添加到 PATH：C:\poppler\Library\bin
```

**macOS：**
```bash
brew install poppler
```

**Linux：**
```bash
sudo apt-get install libpoppler-cpp-dev
```

---

### 第3步：修改 CMakeLists.txt（5分钟）

在 `CMakeLists.txt` 末尾添加（或在依赖部分插入）：

```cmake
# =============================
# Enhanced FileReadTool (JackProAi alignment)
# =================================

option(USE_ENHANCED_FILE_READ "Enable enhanced file read (PDF/Image/Notebook)" ON)
option(USE_POPPLER "Enable native PDF support via poppler-cpp" ON)
option(USE_ASYNC_FILE_READ "Enable async file read (experimental)" OFF)

if(USE_ENHANCED_FILE_READ)
    message(STATUS "Enhanced FileReadTool enabled")

    # stb_image (header-only)
    include_directories(${CMAKE_SOURCE_DIR}/third_party/stb)

    # poppler-cpp (optional)
    if(USE_POPPLER)
        if(WIN32)
          # Windows: manually specify poppler path if needed
            if(EXISTS "C:/poppler/Library")
                set(POPPLER_INCLUDE_DIRS "C:/poppler/Library/include")
                set(POPPLER_LIBRARY_DIRS "C:/poppler/Library/lib")
                set(POPPLER_LIBRARIES "poppler-cpp")
            endif()
        else()
          # Unix: use pkg-config
            find_package(PkgConfig)
            if(PkgConfig_FOUND)
                pkg_check_modules(POPPLER QUIET poppler-cpp)
            endif()
        endif()

        if(POPPLER_FOUND OR EXISTS "C:/poppler/Library")
            include_directories(${POPPLER_INCLUDE_DIRS})
        link_directories(${POPPLER_LIBRARY_DIRS})
            target_compile_definitions(closecrab-unified PRIVATE HAS_POPPLER)
            target_link_libraries(closecrab-unified ${POPPLER_LIBRARIES})
            message(STATUS "Poppler found - native PDF support enabled")
        else()
            message(STATUS "Poppler not found - using external pdftotext fallback")
        endif()
    endif()

    target_compile_definitions(closecrab-unified PRIVATE USE_ENHANCED_FILE_READ)

    if(USE_ASYNC_FILE_READ)
        target_compile_definitions(closecrab-unified PRIVATE ENABLE_ASYNC_FILE_READ)
        message(STATUS "Async file read enabled (experimental)")
    endif()
endif()
```

---

### 第4步：修改工具注册（5分钟）

找到工具注册的地方（通常在 `src/main.cpp` 或 `src/tools/ToolRegistry.cpp`），修改：

```cpp
// 原来的代码：
// #include "tools/FileReadTool/FileReadTool.h"
// registry.registerTool(std::make_unique<FileReadTool>());

// 改为：
#ifdef USE_ENHANCED_FILE_READ
    #ifdef ENABLE_ASYNC_FILE_READ
        #include "tools/FileReadTool/FileReadTool_Async.h"
        registry.registerTool(std::make_unique<FileReadToolAsync>());
    #else
        #include "tools/FileReadTool/FileReadTool_Enhanced.h"
      registry.registerTool(std::make_unique<FileReadToolEnhanced>());
    #endif
#else
    #include "tools/FileReadTool/FileReadTool.h"
    registry.registerTool(std::make_unique<FileReadTool>());
#endif
```

---

### 第5步：添加权限系统初始化（3分钟）

在 `src/main.cpp` 的 `main()` 函数开头添加：

```cpp
#include "core/PermissionManager.h"

int main(int argc, char* argv[]) {
    // ... 其他初始化

#ifdef USE_ENHANCED_FILE_READ
    // 加载权限配置
    namespace fs = std::filesystem;
    fs::path projectRoot = fs::current_path();
    PermissionManager::getInstance().loadPermissions(projectRoot);
#endif

    // ... 启动主循环
}
```

---

### 第6步：编译（5分钟）

```bash
# 清理旧构建
rm -rf build

# 配置（启用 Enhanced，禁用 Async）
cmake -B build -DUSE_ENHANCED_FILE_READ=ON -DUSE_POPPLER=ON -DUSE_ASYNC_FILE_READ=OFF

# 编译
cmake --build build --config Release
```

**如果遇到 poppler 找不到**：
```bash
# 暂时禁用 poppler，使用外部 pdftotext
cmake -B build -DUSE_ENHANCED_FILE_READ=ON -DUSE_POPPLER=OFF
cmake --build build --config Release
```

---

### 第7步：验证编译成功（1分钟）

```bash
./build/closecrab-unified --version

# 或 Windows:
.\build\Release\closecrab-unified.exe --version
```

应该输出版本信息，无错误。

---

### 第8步：测试文本文件（2分钟）

```bash
# 创建测试文件
echo "Hello World" > test.txt

# 启动 CloseCrab
./build/closecrab-unified

# 在提示符下输入：
# Read test.txt
```

应该看到：
```
1    Hello World
```

---

### 第9步：测试图片（3分钟）

```bash
# 准备一张测试图片（或使用现有的）
cp /path/to/screenshot.png test.png

# 在 CloseCrab 中：
# Read test.png
```

应该看到：
```
Image read: test.png (240KB, image/jpeg)
[图片已加载到上下文，可以分析]
```

---

### 第10步：测试 PDF（2分钟）

```bash
# 准备测试 PDF
cp /path/to/document.pdf test.pdf

# 在 CloseCrab 中：
# Read test.pdf pages:1-3
```

**如果安装了 poppler**：
```
=== Page 1 ===
Document Title
...
```

**如果没安装 poppler**：
```
[Error: PDF tools not installed. Install poppler-utils:
  Windows: choco install poppler
  ...
]
```

---

## ✅ 完成检查

测试以下场景，全部通过即为成功：

- [x] 文本文件读取正常
- [x] 图片文件自动识别并加载
- [x] PDF文件能读取（或给出友好错误）
- [x] UNC路径被拒绝（测试：`\\server\share\file.txt`）
- [x] 二进制文件被拒绝（测试：`.exe` 文件）
- [x] 大文件自动截断（测试：大于1MB的文本）
- [x] Notebook文件支持（测试：`.ipynb` 文件）

---

## 📝 可选：创建权限配置

```bash
mkdir -p .closecrab

cat > .closecrab/permissions.json << 'EOF'
{
  "read": {
    "deny": [
    "**/.env",
      "**/*.key",
      "**/*.pem",
      "**/.git/objects/**",
      "**/node_modules/**"
    ]
  },
  "write": {
    "deny": [
      "**/.git/**",
      "**/node_modules/**"
    ]
  }
}
EOF
```

测试：
```bash
# 创建测试文件
echo "SECRET_KEY=12345" > .env

# 尝试读取（应该被拒绝）
# Read .env
```

应该看到：
```
File is in a directory that is denied by your permission settings.
```

---

## 🎉 成功！

如果所有测试通过，你已经成功将 CloseCrab 的 FileReadTool **完全对齐** JackProAi！

---

## 🔧 故障排除

### 问题 1：找不到 stb_image.h
**症状**：
```
fatal error: stb_image.h: No such file or directory
```

**解决**：
```bash
# 确认文件存在
ls third_party/stb/stb_image.h

# 重新配置 CMake
cmake -B build -DUSE_ENHANCED_FILE_READ=ON
```

### 问题 2：poppler 链接错误

**症状**：
```
undefined reference to `poppler::document::load_from_file'
```

**解决**：
```bash
# 选项A：禁用 poppler
cmake -B build -DUSE_POPPLER=OFF

# 选项B：手动指定路径（Windows）
cmake -B build -DCMAKE_PREFIX_PATH="C:/poppler/Library"
```

### 问题 3：编译时内存不足

**症状**：
```
c++: fatal error: Killed (signal 9)
```

**解决**：
```bash
# 限制并行编译
cmake --build build --config Release -- -j2  # 只用2个核心
```

### 问题 4：PDF 读取失败

**症状**：
```
[Error: PDF tools not installed...]
```

**解决**：
```bash
# Windows:
choco install poppler

# 或手动安装 pdftotext
# 下载：https://github.com/oschwartz10612/poppler-windows/releases
# 添加到 PATH
```

---

## 📚 下一步

### 启用异步 I/O（可选）

如果你的使用场景经常需要批量读取文件（5个以上），可以启用异步版本：

```bash
cmake -B build -DUSE_ENHANCED_FILE_READ=ON -DUSE_ASYNC_FILE_READ=ON
cmake --build build --config Release
```

**注意**：启用前阅读 `异步IO详细分析_风险收益_2026-06-05.md`

### 添加测试用例

参考 `实施完成报告_FileReadTool全面对齐_2026-06-05.md` 中的测试清单。

### 性能基准测试

```bash
# 运行 benchmark
./build/closecrab-unified --benchmark file-read
```

---

## 🆘 需要帮助？

检查以下文档：

1. **C++特有问题_FileReadTool对齐报告_2026-06-05.md** - 7个难点详细分析
2. **实施完成报告_FileReadTool全面对齐_2026-06-05.md** - 完整实施清单
3. **异步IO详细分析_风险收益_2026-06-05.md** - 异步版本风险评估

或联系开发者。

---

**祝成功！🚀**

下一步：测试所有功能，然后 commit 代码。
