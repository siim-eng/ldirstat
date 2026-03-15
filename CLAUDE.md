# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ldirstat is a disk usage statistics application (similar to WinDirStat/KDirStat). Fast, low-latency directory scanning and analysis.
Predictable memory usage; minimize heap allocations. Clear separation: core logic (C++ stdlib only) vs UI (Qt-only).
Thread-safe worker agents with small, well-defined IPC/queue contracts.

## Language & Build

The project targets C/C++, g++, ninja, cmake. Use .clang-format, .clang-tidy

## Code Style

- camelCase for all identifiers (variables, functions, parameters).
- Class member variables have trailing underscore (e.g., `entryStore_`, `mutex_`).
- Struct fields have no underscore (e.g., `pageId`, `firstChild`).

## Folder Structure

- `src/core/` — pure C++ stdlib, no Qt. Core types and scanner.
  - `direntry.h` — `DirEntry` struct, `EntryRef` (pageId + index), `EntryType` enum.
  - `namestore.h` — `NameRef` + `NameStore`: page-based (64KB pages) string storage for names.
  - `direntrystore.h` — `DirEntryStore`: page-based arena (65536 entries/page) for DirEntry nodes.
  - `scanner.h/.cpp` — `Scanner`: multi-threaded dir walker using `SYS_getdents64`. Workers share a dir queue, get own store/name pages.
  - `flamegraph.h/.cpp` — `FlameGraph`: builds per-row rect layout from DirEntry tree for flame-graph visualization.
- `src/ui/` — Qt6 Widgets UI layer.
  - `mainwindow.h/.cpp` — `MainWindow`: owns core state, slots for directory selection/scan/flamegraph clicks.
  - `mainwindowbuilder.h/.cpp` — `MainWindowBuilder`: friend class, creates widgets/layout/menus/signal wiring.
  - `dirtreeview.h/.cpp` — `DirTreeView`: QTreeView with lazy-expand directory tree.
  - `filelistview.h/.cpp` — `FileListView`: QTableView showing top 100 files by size in selected subtree.
  - `flamegraphwidget.h/.cpp` — `FlameGraphWidget`: custom QWidget rendering FlameGraph rects with hit testing.
- `src/app/` — application entry point.
  - `main.cpp` — QApplication setup, creates MainWindow.
- `bench/` — benchmarks.
  - `scandirs.cpp` — CLI: `scandirs <rootdir> <worker_count>`, prints dirs/files/disk_used/time.
- Build: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`
