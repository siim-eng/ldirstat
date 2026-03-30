# CLAUDE.md

Short repo notes for coding agents. Prefer code over prose when they disagree.

- Build: `scripts/build.sh` (or `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`)
- Stack: C++20, CMake, Ninja, Qt6 Widgets UI, stdlib-only core, pthread scanner threads
- Targets: `ldirstat_core`, `ldirstat_ui`, `LDirStat`, `scandirs`, `scanmounts`, `gentree`

## Structure

- `scripts/`: repo helper scripts: `build.sh`, `format.sh`, `tidy.sh`, `cppcheck.sh`
- `src/core/`: no Qt; scanner, filesystem info, flamegraph/treemap layout, arena stores
- `src/ui/`: `MainWindow`, builder, dir column browser, graph widgets, scan progress, welcome screen
- `src/app/`: app entry point
- `bench/`: small benchmark tools

## Tooling

- Format `src/`: `scripts/format.sh`
- clang-tidy `src/`: `scripts/tidy.sh`
- cppcheck `src/`: `scripts/cppcheck.sh`

## Current behavior to know

- `DirEntry` is 64 bytes and `EntryType` includes `MountPoint`
- Default scans stay on one filesystem; skipped mount points show `mnt`
- Mount points can be scanned later via **Continue Scanning at Mount Point**
- `Scanner` supports subtree continuation with `continueScan()`, `commitContinueScan()`, and `revertContinueScan()`
- Analysis view is vertical: directory list on top, graph on bottom

## Style

- camelCase for identifiers
- class members use trailing `_`
- struct fields do not use trailing `_`
- keep `src/core` free of Qt
- prefer low-allocation / arena-friendly changes
