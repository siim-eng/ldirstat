# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ldirstat is a disk usage statistics application (similar to WinDirStat/KDirStat). Fast, low-latency directory scanning and analysis.
Predictable memory usage; minimize heap allocations. Clear separation: core logic (C++ stdlib only) vs UI (Qt-only).
Thread-safe worker agents with small, well-defined IPC/queue contracts.

## Language & Build

- C++20, g++, Ninja, CMake 3.20+.
- Qt6 Widgets for UI, pthread for scanner threads.
- CMake targets: `ldirstat_core` (static lib, no Qt), `ldirstat_ui` (static lib, Qt), `ldirstat` (executable), `scandirs`/`scanmounts` (benchmarks).
- Build: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`

## Code Style

- camelCase for all identifiers (variables, functions, parameters).
- Class member variables have trailing underscore (e.g., `entryStore_`, `mutex_`).
- Struct fields have no underscore (e.g., `pageId`, `firstChild`).

## Folder Structure

- `src/core/` — pure C++ stdlib, no Qt. Core types, scanner, and filesystem info.
  - `direntry.h` — `DirEntry` struct, `EntryRef` (pageId + index), `EntryType` enum.
  - `namestore.h` — `NameRef` + `NameStore`: page-based (64KB pages) string storage for names.
  - `direntrystore.h` — `DirEntryStore`: page-based arena (65536 entries/page) for DirEntry nodes.
  - `scanner.h/.cpp` — `Scanner`: multi-threaded dir walker using `SYS_getdents64`. Workers share a dir queue, get own store/name pages. Scans single device only (`rootDev_`). Stoppable via `stop()`.
  - `flamegraph.h/.cpp` — `FlameGraph`: builds per-row rect layout from DirEntry tree for flame-graph visualization. Binary search hit testing.
  - `filesystem.h/.cpp` — `FileSystems`: reads `/proc/mounts`, classifies filesystem types (Real, Network, Virtual, etc.), provides mount lookup by device. `MountInfo` struct with device, mountPoint, fsType, capacity.
- `src/ui/` — Qt6 Widgets UI layer.
  - `mainwindow.h/.cpp` — `MainWindow`: owns core state (FileSystems, DirEntryStore, NameStore, FlameGraph). Background scanning via QThread. QStackedWidget switches between welcome screen and analysis view.
  - `mainwindowbuilder.h/.cpp` — `MainWindowBuilder`: friend class, creates widgets/layout/menus/signal wiring. Layout: top splitter (dir tree 30% | file list 70%), flamegraph full width below.
  - `dirtreeview.h/.cpp` — `DirTreeView`: QTreeView with lazy-expand directory tree.
  - `filelistview.h/.cpp` — `FileListView`: QTableView showing top 100 files by size in selected subtree.
  - `flamegraphwidget.h/.cpp` — `FlameGraphWidget`: custom QWidget rendering FlameGraph rects with hit testing and tooltips.
  - `welcomewidget.h/.cpp` — `WelcomeWidget`: initial landing screen with quick-access buttons (Home, Root, Open Directory) and a filesystem selection grid.
- `src/app/` — application entry point.
  - `main.cpp` — QApplication setup, creates MainWindow.
- `bench/` — benchmarks.
  - `scandirs.cpp` — CLI: `scandirs <rootdir> <worker_count>`, prints dirs/files/disk_used/time.
  - `scanmounts.cpp` — CLI: `scanmounts`, reads and displays all mount points with filesystem classification and capacity.
