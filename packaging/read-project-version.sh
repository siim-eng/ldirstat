#!/usr/bin/env bash
set -euo pipefail

scriptDir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repoRoot=$(cd "${scriptDir}/.." && pwd)

version=$(sed -nE 's/^project\(ldirstat VERSION ([0-9]+(\.[0-9]+)*) LANGUAGES CXX\)$/\1/p' \
    "${repoRoot}/CMakeLists.txt")

if [[ -z "${version}" ]]; then
    echo "Failed to read project version from CMakeLists.txt" >&2
    exit 1
fi

printf '%s\n' "${version}"
