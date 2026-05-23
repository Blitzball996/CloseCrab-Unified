# CloseCrab AI - JetBrains Plugin

AI coding assistant for IntelliJ IDEA, PyCharm, CLion, WebStorm, and all JetBrains IDEs.

## Features
- Right-click context menu: Explain/Fix/Refactor/Test
- Chat tool window (sidebar)
- Connects to CloseCrab-Unified backend

## Requirements
- CloseCrab-Unified running on localhost:9001
- JetBrains IDE 2023.3+

## Build
```bash
./gradlew buildPlugin
```

## Install
1. Build the plugin: `./gradlew buildPlugin`
2. In IDE: Settings → Plugins → Install from disk
3. Select `build/distributions/closecrab-*.zip`
