#!/usr/bin/env bash
set -euo pipefail

scriptDir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repoRoot=$(cd "${scriptDir}/.." && pwd)
cmakeBin="${CMAKE_BIN:-cmake}"

buildDir="${BUILD_DIR:-${repoRoot}/build/appimage}"
appDir="${APPDIR:-${buildDir}/AppDir}"
distDir="${DIST_DIR:-${repoRoot}/dist}"
toolsDir="${TOOLS_DIR:-${repoRoot}/build/appimage-tools}"

projectVersion="$("${scriptDir}/read-project-version.sh")"
arch=$(uname -m)

if [[ "${arch}" != "x86_64" ]]; then
    echo "This AppImage build currently supports x86_64 only." >&2
    exit 1
fi

determine_release_date() {
    if [[ -n "${LDIRSTAT_RELEASE_DATE:-}" ]]; then
        printf '%s\n' "${LDIRSTAT_RELEASE_DATE}"
        return
    fi

    local tag="v${projectVersion}"
    git -C "${repoRoot}" rev-parse --verify "${tag}^{commit}" >/dev/null 2>&1 &&
        git -C "${repoRoot}" log -1 --format=%cs "$tag" && return

    date -u +%F
}

releaseDate=$(determine_release_date)

mkdir -p "${buildDir}" "${distDir}" "${toolsDir}"
rm -rf "${appDir}"

download_tool() {
    local url=$1
    local destination=$2

    if [[ -x "${destination}" ]]; then
        return
    fi

    if command -v curl >/dev/null 2>&1; then
        curl -L --fail --output "${destination}" "${url}"
    elif command -v wget >/dev/null 2>&1; then
        wget -O "${destination}" "${url}"
    else
        echo "Need curl or wget to download AppImage tooling." >&2
        exit 1
    fi

    chmod +x "${destination}"
}

linuxdeploy="${toolsDir}/linuxdeploy-x86_64.AppImage"
linuxdeployQtPlugin="${toolsDir}/linuxdeploy-plugin-qt-x86_64.AppImage"
appimagetool="${toolsDir}/appimagetool-x86_64.AppImage"
runtimeFile="${toolsDir}/runtime-x86_64"

download_tool \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" \
    "${linuxdeploy}"
download_tool \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage" \
    "${linuxdeployQtPlugin}"
download_tool \
    "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage" \
    "${appimagetool}"
download_tool \
    "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64" \
    "${runtimeFile}"

qmakeBinary=""
if command -v qmake6 >/dev/null 2>&1; then
    qmakeBinary=$(command -v qmake6)
elif command -v qmake >/dev/null 2>&1; then
    qmakeBinary=$(command -v qmake)
fi

qtPluginBase=""
if [[ -n "${qmakeBinary}" ]]; then
    qtPluginBase=$("${qmakeBinary}" -query QT_INSTALL_PLUGINS)
fi

deploy_qt_plugins() {
    local pluginSubdir=$1
    local sourceDir="${qtPluginBase}/${pluginSubdir}"
    local targetDir="${appDir}/usr/plugins/${pluginSubdir}"

    if [[ -z "${qtPluginBase}" ]] || [[ ! -d "${sourceDir}" ]]; then
        return
    fi

    shopt -s nullglob
    local pluginFiles=("${sourceDir}"/*.so)
    shopt -u nullglob
    if [[ ${#pluginFiles[@]} -eq 0 ]]; then
        return
    fi

    mkdir -p "${targetDir}"

    for pluginFile in "${pluginFiles[@]}"; do
        local pluginName
        pluginName=$(basename "${pluginFile}")

        install -D -m 0755 "${pluginFile}" "${targetDir}/${pluginName}"
        "${linuxdeploy}" \
            --appdir "${appDir}" \
            --deploy-deps-only "${targetDir}/${pluginName}"
    done
}

"${cmakeBin}" -S "${repoRoot}" \
      -B "${buildDir}" \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr \
      -DLDIRSTAT_RELEASE_DATE="${releaseDate}"

"${cmakeBin}" --build "${buildDir}" --target LDirStat
DESTDIR="${appDir}" "${cmakeBin}" --install "${buildDir}" --component App

cp "${appDir}/usr/share/metainfo/io.github.siim_eng.ldirstat.desktop.metainfo.xml" \
   "${appDir}/usr/share/metainfo/io.github.siim_eng.ldirstat.desktop.appdata.xml"

outputAppImage="${distDir}/LDirStat-${projectVersion}-x86_64.AppImage"
rm -f "${outputAppImage}"

(
    cd "${repoRoot}"
    export APPIMAGE_EXTRACT_AND_RUN=1
    export ARCH=x86_64
    export EXTRA_QT_MODULES=svg
    export PATH="${toolsDir}:${PATH}"
    export VERSION="${projectVersion}"

    if [[ -n "${qmakeBinary}" ]]; then
        export QMAKE="${qmakeBinary}"
    fi

    "${linuxdeploy}" \
        --appdir "${appDir}" \
        --executable "${appDir}/usr/bin/LDirStat" \
        --desktop-file "${appDir}/usr/share/applications/ldirstat.desktop" \
        --icon-file "${appDir}/usr/share/icons/hicolor/scalable/apps/ldirstat.svg" \
        --icon-file "${appDir}/usr/share/icons/hicolor/256x256/apps/ldirstat.png" \
        --plugin qt

    deploy_qt_plugins platformthemes
    deploy_qt_plugins styles

    if [[ -n "${qtPluginBase}" ]] && [[ -f "${qtPluginBase}/platforms/libqoffscreen.so" ]]; then
        install -D -m 0755 \
            "${qtPluginBase}/platforms/libqoffscreen.so" \
            "${appDir}/usr/plugins/platforms/libqoffscreen.so"
        "${linuxdeploy}" \
            --appdir "${appDir}" \
            --deploy-deps-only "${appDir}/usr/plugins/platforms/libqoffscreen.so"
    fi

    "${appimagetool}" \
        --no-appstream \
        --runtime-file "${runtimeFile}" \
        "${appDir}" \
        "${outputAppImage}"
)

printf 'AppImage written to %s\n' "${outputAppImage}"
