# CloseCrab AI - Visual Studio Extension

AI coding assistant for Visual Studio 2019/2022 (all editions).

## Features
- Chat tool window (View > Other Windows > CloseCrab AI)
- Right-click: Explain/Fix/Review selected code
- Connects to CloseCrab-Unified backend

## Supported Versions
- Visual Studio 2019 (16.x)
- Visual Studio 2022 (17.x)
- Community, Professional, Enterprise editions

## Requirements
- CloseCrab-Unified running on localhost:9001
- .NET Framework 4.7.2+

## Build
Open `CloseCrab.sln` in Visual Studio, build in Release mode.
Output: `CloseCrab\bin\Release\CloseCrab.vsix`

## Install
Double-click the .vsix file, or:
Extensions > Manage Extensions > Install from file
