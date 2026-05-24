# Contributing to CloseCrab-Unified

Thanks for your interest in contributing! Here's how to get started.

## Quick Start

```bash
git clone https://github.com/Blitzball996/CloseCrab-Unified.git
cd CloseCrab-Unified
cmake -B build -DBUILD_TESTS=ON
cmake --build build --config Release
```

## What We Need Help With

- **Bug reports** — Especially on Linux/macOS (primary dev is on Windows)
- **Tool implementations** — New tools following the pattern in `src/tools/`
- **Model testing** — Try different GGUF models and report results
- **Documentation** — Improve user guide, add examples
- **Translations** — Help translate docs to other languages

## Code Style

- C++17, no exceptions in hot paths
- Use `spdlog` for logging
- Follow existing naming: `PascalCase` for classes, `camelCase` for methods
- Keep files under 500 lines

## Pull Request Process

1. Fork the repo and create a branch from `main`
2. Make your changes
3. Run tests: `cmake --build build --target closecrab-tests`
4. Submit a PR with a clear description of what and why

## Reporting Bugs

Use the [bug report template](https://github.com/Blitzball996/CloseCrab-Unified/issues/new?template=bug_report.md) or just open an issue with:
- What you expected
- What happened
- Steps to reproduce
- OS, GPU, model info

## License

By contributing, you agree that your contributions will be licensed under MIT.
