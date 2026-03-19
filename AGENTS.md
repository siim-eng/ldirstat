# CLAUDE.md

Agent notes for this repository. Prefer current code over stale prose.

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
  - `direntry.h` — `DirEntry` is a fixed 48-byte node: name ref, type, size/blocks, subtree counts, and tree links. No stored depth/device/inode.
  - `namestore.h` — `NameRef` + `NameStore`: page-based (64KB pages) string storage for names.
  - `direntrystore.h` — `DirEntryStore`: page-based arena (32768 entries/page) for DirEntry nodes.
  - `scanner.h/.cpp` — `Scanner`: multi-threaded dir walker using `SYS_getdents64`. Workers share a dir queue, get own store/name pages. Same-device filtering uses transient `stat.st_dev`; entries do not retain device/inode. Stoppable via `stop()`.
  - `flamegraph.h/.cpp` — `FlameGraph`: builds per-row rect layout from DirEntry tree for flame-graph visualization. Binary search hit testing.
  - `filesystem.h/.cpp` — `FileSystems`: reads `/proc/mounts`, classifies filesystem types (Real, Network, Virtual, etc.), provides mount lookup by device. `MountInfo` struct with device, mountPoint, fsType, capacity.
- `src/ui/` — Qt6 Widgets UI layer.
  - `mainwindow.h/.cpp` — `MainWindow`: owns core state (`FileSystems`, stores, `Scanner`), scan thread, mount process, current selection/focus, and toolbar actions (open, terminal, copy path, trash).
  - `mainwindowbuilder.h/.cpp` — `MainWindowBuilder`: builds the hidden toolbar, welcome page, analysis splitter, scan-progress stack, and graph-type menu. Central UI is `viewStack_` = welcome or analysis.
  - `dirlistview.h/.cpp` — horizontal column browser for the directory hierarchy with keyboard navigation and context-menu forwarding.
  - `dirlistcolumn.h/.cpp` — custom painted single-directory column with size, percent, name, and footer totals.
  - `graphwidget.h/.cpp` — abstract base API shared by graph views.
  - `flamegraphwidget.h/.cpp` — flame graph view with hit testing and subtree highlight contour.
  - `treemapwidget.h/.cpp` — tree map view; supports packed mode and directory-header mode.
  - `scanprogresswidget.h/.cpp` — indeterminate scan progress page with live file/dir counters and Stop button.
  - `welcomewidget.h/.cpp` — landing page with Home/Root/Open Directory actions and a filesystem grid; unmounted devices go through `udisksctl mount`.
- `src/app/` — application entry point.
  - `main.cpp` — QApplication setup, creates MainWindow.
- `bench/` — benchmarks.
  - `gentree.cpp` — CLI: `gentree <file_count>`, generates a small-file benchmark tree.
  - `scandirs.cpp` — CLI: `scandirs <rootdir> <worker_count>`, prints dirs/files/disk_used/time.
  - `scanmounts.cpp` — CLI: `scanmounts`, reads and displays all mount points with filesystem classification and capacity.
