#!/bin/bash
set -euo pipefail

usage() {
    echo "Usage: $0 [--check]"
    echo
    echo "Formats C/C++ source files under src/ using the repo's .clang-format."
    echo "  --check   Verify formatting without modifying files"
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    usage
    exit 0
fi

mode="format"
if [[ "${1:-}" == "--check" ]]; then
    mode="check"
elif [[ $# -ne 0 ]]; then
    usage >&2
    exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir"

if ! command -v clang-format >/dev/null 2>&1; then
    echo "clang-format not found in PATH" >&2
    exit 1
fi

files=()
if command -v rg >/dev/null 2>&1; then
    while IFS= read -r -d '' file; do
        files+=("$file")
    done < <(rg --files src -g '*.h' -g '*.hh' -g '*.hpp' -g '*.cpp' -g '*.cc' -g '*.cxx' -0)
else
    while IFS= read -r -d '' file; do
        files+=("$file")
    done < <(find src -type f \( -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \) -print0)
fi

if [[ ${#files[@]} -eq 0 ]]; then
    echo "No source files found under src/"
    exit 0
fi

if [[ "$mode" == "check" ]]; then
    clang-format --dry-run --Werror "${files[@]}"
    echo "clang-format check passed for ${#files[@]} files in src/"
    exit 0
fi

clang-format -i "${files[@]}"
echo "Formatted ${#files[@]} files in src/"
