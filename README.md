# ldirstat

LDirStat helps you see what is taking up space on your disk.

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

### Distribution Notes

- The AppImage bundles the Qt runtime for `LDirStat` only. Benchmark tools are not included.
- Mounting removable devices still depends on the host providing `udisksctl` from `udisks2`.
- Opening a terminal still depends on a host terminal emulator such as `konsole`,
  `gnome-terminal`, `xfce4-terminal`, or `xterm`.
