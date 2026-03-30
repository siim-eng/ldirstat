#!/bin/bash
set -e
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
cd "$repo_root"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
