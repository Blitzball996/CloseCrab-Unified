# CloseCrab-Unified 用户指南

## 开始之前

### 什么是 CloseCrab？

CloseCrab 是一个运行在你电脑上的 AI 编程助手。你在终端里跟它对话，它可以帮你：

- 写代码、改代码
- 读文件、搜索文件
- 运行命令
- 管理 Git
- 回答编程问题

它可以用本地模型（不需要网络），也可以连接 Claude、OpenAI 等云端 API。

### 电脑配置要求

| 项目 | 最低配置 | 推荐配置 |
|------|----------|----------|
| 操作系统 | Windows 10 / macOS 12 / Ubuntu 20.04 | Windows 11 / macOS 14 / Ubuntu 22.04 |
| 内存 | 8 GB | 16 GB 或更多 |
| 硬盘空间 | 2 GB（仅 API 模式） | 20 GB（本地模型） |
| 显卡 | 不需要（仅 API 模式） | NVIDIA 8GB+ 显存（本地模型） |
| 网络 | 使用 API 时需要网络 | 稳定的网络连接 |

如果你只用 API 模式（连接 Claude 或 OpenAI），不需要好显卡，普通电脑就行。

如果你想用本地模型（不联网），需要一块好显卡（NVIDIA，至少 8GB 显存）。

### 下载地址

- GitHub: https://github.com/Blitzball996/CloseCrab-Unified
- Releases 页面有编译好的安装包

---

## 安装

### Windows 安装

1. 打开浏览器，进入 GitHub Releases 页面
2. 下载 `closecrab-unified-windows-x64.zip`
3. 右键点击下载的 zip 文件，选择「全部解压缩」
4. 把解压出来的文件夹放到你喜欢的位置（比如 `C:\Program Files\CloseCrab`）
5. 把文件夹路径加到系统 PATH：
   - 右键「此电脑」→「属性」→「高级系统设置」→「环境变量」
   - 在「系统变量」里找到 `Path`，双击
   - 点「新建」，输入 CloseCrab 所在的文件夹路径
   - 点「确定」保存

### macOS 安装

1. 下载 `closecrab-unified-macos.dmg`
2. 双击打开 dmg 文件
3. 把 CloseCrab 图标拖到 Applications 文件夹
4. 打开终端，运行：

```bash
sudo ln -s /Applications/CloseCrab.app/Contents/MacOS/closecrab-unified /usr/local/bin/closecrab-unified
```

### Linux 安装

1. 下载 `closecrab-unified-linux-x64.AppImage`
2. 打开终端，进入下载目录：

```bash
cd ~/Downloads
```

3. 给文件添加运行权限：

```bash
chmod +x closecrab-unified-linux-x64.AppImage
```

4. 移动到系统路径（这样在任何地方都能运行）：

```bash
sudo mv closecrab-unified-linux-x64.AppImage /usr/local/bin/closecrab-unified
```

---

## 第一次使用

### 第一步：打开终端

**Windows：**
- 按 `Win + R`，输入 `cmd`，按回车
- 或者：按 `Win` 键，搜索「终端」或「PowerShell」，点击打开

**macOS：**
- 按 `Cmd + 空格`，输入 `Terminal`，按回车
- 或者：打开「应用程序」→「实用工具」→「终端」

**Linux：**
- 按 `Ctrl + Alt + T`
- 或者：在应用菜单里搜索「Terminal」

### 第二步：启动 CloseCrab

在终端里输入：

```bash
closecrab-unified
```

按回车。你会看到欢迎界面。

### 第三步：选择模式

CloseCrab 支持两种模式：

**模式 A：本地模型（不需要网络）**

```bash
closecrab-unified --provider local --model /path/to/your/model.gguf
```

把 `/path/to/your/model.gguf` 换成你下载的模型文件路径。

**模式 B：API 模式（需要网络）**

使用 Claude API：

```bash
closecrab-unified --provider anthropic --api-key sk-ant-xxxxx --api-model claude-sonnet-4-20250514
```

使用 OpenAI API：

```bash
closecrab-unified --provider openai --api-key sk-xxxxx --api-model gpt-4o
```

把 `sk-ant-xxxxx` 或 `sk-xxxxx` 换成你自己的 API Key。

### 第四步：配置 API Key

你也可以把 API Key 写在配置文件里，这样每次启动就不用输入了。

1. 在你的项目文件夹里创建 `config/config.yaml` 文件
2. 写入以下内容：

```yaml
provider: anthropic
api:
  key: sk-ant-你的key
  model: claude-sonnet-4-20250514
  base_url: https://api.anthropic.com
```

然后直接运行 `closecrab-unified` 就行了。

---

## 基本命令

在 CloseCrab 里，所有命令都以 `/` 开头。

### /help - 查看帮助

显示所有可用命令的列表。

```
> /help
```

### /model - 切换模型

查看当前模型：

```
> /model
Current model: claude-sonnet-4-20250514
```

切换到另一个模型：

```
> /model gpt-4o
Model set to: gpt-4o
```

### /clear - 清空对话

清除当前对话的所有历史记录，重新开始。

```
> /clear
Conversation cleared.
```

### /compact - 压缩历史

当对话太长时，压缩历史记录来节省内存和 token。

```
> /compact
```

### /session - 管理会话

查看当前会话信息：

```
> /session
```

### /status - 查看状态

显示当前会话的详细状态，包括模型、消息数、花费等。

```
> /status
```

### /tools - 查看工具列表

显示 AI 可以使用的所有工具（读文件、写文件、运行命令等）。

```
> /tools
```

### /permissions - 权限管理

查看当前权限模式：

```
> /permissions
Permission mode: default
```

切换权限模式：

```
> /permissions auto
> /permissions bypass
> /permissions default
```

- `default`：每次执行危险操作前都会问你
- `auto`：自动允许安全操作
- `bypass`：允许所有操作（小心使用！）

### /memory - 记忆系统

CloseCrab 可以记住你的偏好和项目信息。

```
> /memory
```

### /cost - 查看花费

显示 API 调用的费用统计。

```
> /cost
```

### /audit - 查看审计日志

显示权限操作的历史记录。

```
> /audit
```

### /quit - 退出

退出 CloseCrab。也可以用 `/exit` 或 `/q`。

```
> /quit
```

### 运行 Shell 命令

在输入前加 `!` 可以直接运行系统命令：

```
> !git status
> !ls -la
> !npm install
```

---

## Team Mode 使用

Team Mode 让多个人可以连接到同一台电脑上的 CloseCrab。

### 怎么让多个人连到同一台电脑

1. 在服务器电脑上启动 CloseCrab（开启 WebSocket 服务）：

```bash
closecrab-unified
```

CloseCrab 会在 9002 端口启动 WebSocket 服务。

2. 其他人通过 CloseCrab-Web（手机端）连接到这台电脑。详见 CloseCrab-Web 用户指南。

### 怎么看排行榜

在 CloseCrab-Web 的 Team 页面可以看到：

- 每个人的分数
- 排名
- 获得的徽章

### 怎么搜索团队知识库

CloseCrab 内置了 RAG（检索增强生成）系统。它会自动索引你的代码库，团队成员可以搜索共享的知识。

```
> /rag search 怎么配置数据库连接
```

---

## 常见问题

### 模型加载失败怎么办

1. 检查模型文件路径是否正确
2. 检查文件是否完整（没有下载中断）
3. 检查显存是否足够：

```bash
nvidia-smi
```

4. 尝试减少 GPU 层数：

在 `config/config.yaml` 里设置：

```yaml
gpu:
  layers: 20
```

数字越小，用的显存越少。

### API 报错怎么办

1. 检查 API Key 是否正确
2. 检查网络连接
3. 检查 API 余额是否充足
4. 常见错误码：
   - `401`：API Key 无效
   - `429`：请求太频繁，等一会再试
   - `500`：服务器错误，稍后重试

### 内存不够怎么办

1. 用 `/compact` 压缩对话历史
2. 用 `/clear` 清空对话重新开始
3. 关闭其他占内存的程序
4. 如果用本地模型，换一个更小的模型

### 怎么更新

**Windows / Linux：**

下载最新版本，替换旧文件即可。

**macOS：**

下载新的 dmg，重新拖到 Applications 覆盖旧版本。

---

## 快捷键

| 快捷键 | 功能 |
|--------|------|
| `Enter` | 发送消息 |
| `Ctrl + C` | 中断当前操作 |
| `Ctrl + D` | 退出程序 |
| `Ctrl + L` | 清屏 |
| `Tab` | 自动补全命令 |
| `↑` / `↓` | 浏览历史输入 |
| `!命令` | 运行 Shell 命令 |

---

## 配置文件参考

配置文件位置：`config/config.yaml`

```yaml
# 模型提供商：local, anthropic, openai
provider: anthropic

# API 设置
api:
  key: your-api-key-here
  model: claude-sonnet-4-20250514
  base_url: https://api.anthropic.com

# 本地模型设置
local:
  model_path: /path/to/model.gguf

# GPU 设置（本地模型）
gpu:
  layers: -1        # -1 = 全部放显卡，数字 = 指定层数
  batch_size: 512
  threads: 0        # 0 = 自动检测

# 权限模式：default, auto, bypass
permissions: default
```

---

## 需要更多帮助？

- GitHub Issues: https://github.com/Blitzball996/CloseCrab-Unified/issues
- 在 CloseCrab 里输入 `/help` 查看命令帮助
