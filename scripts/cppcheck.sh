#!/bin/bash
set -euo pipefail

usage() {
    echo "Usage: $0 [--all] [--raw]"
    echo
    echo "Runs cppcheck for this project with Qt-aware settings."
    echo "By default only files under src/ are analyzed."
    echo "  --all   Analyze the whole compile database instead of only src/"
    echo "  --raw   Show cppcheck output without the default file filter summary behavior"
}

all=false
raw=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --all)
            all=true
            ;;
        --raw)
            raw=true
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            usage >&2
            exit 1
            ;;
    esac
    shift
done

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
cd "$repo_root"

if ! command -v cppcheck >/dev/null 2>&1; then
    echo "cppcheck not found in PATH" >&2
    exit 1
fi

if [[ ! -f build/compile_commands.json ]]; then
    echo "build/compile_commands.json not found; configure the project first" >&2
    exit 1
fi

mkdir -p build/cppcheck-src

args=(
    --project=build/compile_commands.json
    --cppcheck-build-dir=build/cppcheck-src
    --suppressions-list=.cppcheck-suppressions
    --enable=warning,style,performance,portability
    --inline-suppr
    --quiet
    --library=qt
    -Dslots=
    -Dsignals=public
    -Demit=
    -DQ_OBJECT=
)

if [[ "$all" != true ]]; then
    args+=(--file-filter='*src/*')
fi

if [[ "$raw" == true ]]; then
    cppcheck "${args[@]}"
    exit $?
fi

cppcheck "${args[@]}"
