# LDirStat

LDirStat helps you see what is taking up space on your disk.

![Screenshot of app](docs/images/main.png)

[App Help](docs/HELP.md)

## Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target LDirStat
```

## AppImage

Build an AppImage for the GUI application only:

```bash
./packaging/build-appimage.sh
```

The script stages an AppDir via `cmake --install --component App`, bundles Qt
with `linuxdeploy`, and writes the result to `dist/LDirStat-<version>-x86_64.AppImage`.

## Releasing

1. Update the version in `CMakeLists.txt`:
   ```
   project(ldirstat VERSION 0.3.0 LANGUAGES CXX)
   ```
2. Commit, tag, and push:
   ```bash
   git add CMakeLists.txt
   git commit -m "v0.3.0"
   git tag v0.3.0
   git push origin main v0.3.0
   ```

The tag must match the project version (`v<VERSION>`). Pushing a `v*` tag
triggers the [Release AppImage](.github/workflows/release-appimage.yml)
workflow, which builds an AppImage and publishes a GitHub release.

### Distribution Notes

- The AppImage bundles the Qt runtime for `LDirStat` only. Benchmark tools are not included.
- Mounting removable devices still depends on the host providing `udisksctl` from `udisks2`.
- Opening a terminal still depends on a host terminal emulator such as `konsole`,
  `gnome-terminal`, `xfce4-terminal`, or `xterm`.
