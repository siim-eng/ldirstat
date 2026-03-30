#!/bin/bash
set -euo pipefail

usage() {
    echo "Usage: $0 [--fix] [--quiet] [--headers] [--raw]"
    echo
    echo "Runs clang-tidy on C/C++ files under src/ using the repo's .clang-tidy."
    echo "By default it analyzes translation units only (.cpp/.cc/.cxx)."
    echo "  --fix     Apply clang-tidy fixes"
    echo "  --quiet   Pass --quiet to clang-tidy"
    echo "  --headers Also run directly on header files"
    echo "  --raw     Show clang-tidy's warning-count boilerplate"
}

fix=false
quiet=false
headers=false
raw=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --fix)
            fix=true
            ;;
        --quiet)
            quiet=true
            ;;
        --headers)
            headers=true
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
cd "$script_dir"

if ! command -v clang-tidy >/dev/null 2>&1; then
    echo "clang-tidy not found in PATH" >&2
    exit 1
fi

if [[ ! -f build/compile_commands.json ]]; then
    echo "build/compile_commands.json not found; configure the project first" >&2
    exit 1
fi

files=()
if command -v rg >/dev/null 2>&1; then
    while IFS= read -r -d '' file; do
        files+=("$file")
    done < <(rg --files src -g '*.cpp' -g '*.cc' -g '*.cxx' -0)
    if [[ "$headers" == true ]]; then
        while IFS= read -r -d '' file; do
            files+=("$file")
        done < <(rg --files src -g '*.h' -g '*.hh' -g '*.hpp' -0)
    fi
else
    while IFS= read -r -d '' file; do
        files+=("$file")
    done < <(find src -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \) -print0)
    if [[ "$headers" == true ]]; then
        while IFS= read -r -d '' file; do
            files+=("$file")
        done < <(find src -type f \( -name '*.h' -o -name '*.hh' -o -name '*.hpp' \) -print0)
    fi
fi

if [[ ${#files[@]} -eq 0 ]]; then
    echo "No source files found under src/"
    exit 0
fi

args=(-p build --config-file=.clang-tidy)
if [[ "$quiet" == true ]]; then
    args+=(--quiet)
fi
if [[ "$fix" == true ]]; then
    args+=(--fix)
fi

if [[ "$raw" == true ]]; then
    clang-tidy "${args[@]}" "${files[@]}"
    exit $?
fi

set +e
clang-tidy "${args[@]}" "${files[@]}" 2>&1 | awk '
    /^[0-9]+ warnings generated\.$/ { next }
    /^Suppressed [0-9]+ warnings \([0-9]+ in non-user code\)\.$/ { next }
    /^Use -header-filter=.*$/ { next }
    { print }
'
status=${PIPESTATUS[0]}
set -e
exit $status
