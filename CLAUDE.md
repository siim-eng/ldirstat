# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ldirstat is a disk usage statistics application (similar to WinDirStat/KDirStat). Fast, low-latency directory scanning and analysis.
Predictable memory usage; minimize heap allocations. Clear separation: core logic (C++ stdlib only) vs UI (Qt-only).
Thread-safe worker agents with small, well-defined IPC/queue contracts.

## Language & Build

The project targets C/C++, g++, ninja, cmake. Use .clang-format, .clang-tidy

## Code Style

- All class member variables must have a trailing underscore (e.g. `pages_`, `mutex_`).

## Folder Structure

```
ldirstat/
├── CMakeLists.txt              # top-level: project options, subdirs
├── cmake/                      # CMake modules/helpers
│   └── CompilerWarnings.cmake
├── .clang-format
├── .clang-tidy
│
├── src/
│   ├── core/                   # C++ stdlib only — no Qt
│   │   ├── CMakeLists.txt
│   │   ├── scanner.h/.cpp      # filesystem walker (readdir/getdents64)
│   │   ├── direntry.h          # node/tree types (arena-friendly)
│   │   ├── dirtree.h/.cpp      # tree construction & size aggregation
│   │   ├── workqueue.h         # lock-free or mutex-based task queue
│   │   └── worker.h/.cpp       # thread-pool / agent logic
│   │
│   └── ui/                     # Qt only — depends on core
│       ├── CMakeLists.txt
│       ├── main.cpp
│       ├── mainwindow.h/.cpp
│       ├── treemapwidget.h/.cpp   # treemap visualization
│       └── dirmodel.h/.cpp        # QAbstractItemModel adapter
│
└── tests/
    ├── CMakeLists.txt
    ├── test_scanner.cpp
    └── test_dirtree.cpp
```

- `src/core/` — pure C++ stdlib. Compiles as a static library. No Qt headers.
- `src/ui/` — Qt-only. Thin adapter layer consuming core types.
- `cmake/` — shared compiler flags, sanitizer toggles.
- `tests/` — links against core library directly; no Qt needed for core tests.
