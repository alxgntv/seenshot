#!/bin/zsh
set -euo pipefail

# ─── Ariadne's Thread [AT-0197] ─────────────────────
# What: Local install of SeenShot.app over /Applications after a cmake build
# Why:  Dock and login item always launch /Applications, not build/
# Date: 2026-08-27
# Related: [AT-0120] packaging/macos/package_sparkle.sh, .cursor/rules/rebuild-macos-app.mdc
# ─────────────────────────────────────────────────────

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${ROOT}/build"
APP="${BUILD}/SeenShot.app"
ENTITLEMENTS="${ROOT}/packaging/macos/SeenShot.entitlements"
QT_PREFIX="${QT_PREFIX:-$(brew --prefix qtbase)}"
MACDEPLOYQT="${MACDEPLOYQT:-${QT_PREFIX}/bin/macdeployqt}"
if [[ ! -x "${MACDEPLOYQT}" ]]; then
  MACDEPLOYQT="$(brew --prefix qttools)/bin/macdeployqt"
fi

log() {
  echo "install_local: $*"
}

fail() {
  echo "install_local: ERROR $*" >&2
  exit 1
}

IDENTITY="${CODESIGN_IDENTITY:-}"
if [[ -z "${IDENTITY}" ]]; then
  IDENTITY="$(security find-identity -v -p codesigning | awk -F'\"' '/Developer ID Application/{print $2; exit}')"
fi
[[ -n "${IDENTITY}" ]] || fail "no Developer ID Application identity"
log "identity=${IDENTITY}"

killall -9 SeenShot 2>/dev/null || true
sleep 0.3

log "cmake configure"
cmake -S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="${QT_PREFIX}"
log "cmake build"
cmake --build "${BUILD}" --config Release -j"$(sysctl -n hw.ncpu)"
[[ -d "${APP}" ]] || fail "app bundle missing ${APP}"
[[ -x "${MACDEPLOYQT}" ]] || fail "macdeployqt missing"

log "macdeployqt"
"${MACDEPLOYQT}" "${APP}" -always-overwrite
log "restore Sparkle.framework"
rm -rf "${APP}/Contents/Frameworks/Sparkle.framework"
mkdir -p "${APP}/Contents/Frameworks"
cp -R "${ROOT}/third_party/Sparkle/Sparkle.framework" "${APP}/Contents/Frameworks/"
if otool -l "${APP}/Contents/MacOS/SeenShot" | grep -q '/opt/homebrew/opt/qtbase/lib'; then
  log "delete homebrew rpath"
  /usr/bin/install_name_tool -delete_rpath /opt/homebrew/opt/qtbase/lib "${APP}/Contents/MacOS/SeenShot" || true
fi

sign_nested() {
  local path="$1"
  log "codesign ${path}"
  /usr/bin/codesign --force --sign "${IDENTITY}" --options runtime --timestamp "${path}"
}

if [[ -d "${APP}/Contents/PlugIns" ]]; then
  find "${APP}/Contents/PlugIns" -type f \( -name '*.dylib' -o -name '*.so' \) -print | while read -r plug; do
    sign_nested "${plug}"
  done
fi
if [[ -d "${APP}/Contents/Frameworks" ]]; then
  find "${APP}/Contents/Frameworks" -type f -name '*.dylib' -print | while read -r dylib; do
    sign_nested "${dylib}"
  done
  SPARKLE_FW="${APP}/Contents/Frameworks/Sparkle.framework"
  SPARKLE_VER="${SPARKLE_FW}/Versions/B"
  if [[ -d "${SPARKLE_VER}" ]]; then
    sign_nested "${SPARKLE_VER}/XPCServices/Downloader.xpc"
    sign_nested "${SPARKLE_VER}/XPCServices/Installer.xpc"
    sign_nested "${SPARKLE_VER}/Autoupdate"
    sign_nested "${SPARKLE_VER}/Updater.app"
    sign_nested "${SPARKLE_FW}"
  fi
  find "${APP}/Contents/Frameworks" -name '*.framework' -maxdepth 1 -print | while read -r fw; do
    if [[ "$(basename "${fw}")" == "Sparkle.framework" ]]; then
      continue
    fi
    sign_nested "${fw}"
  done
fi

log "codesign app"
/usr/bin/codesign --force --sign "${IDENTITY}" --options runtime --timestamp \
  --entitlements "${ENTITLEMENTS}" "${APP}"
/usr/bin/codesign --verify "${APP}" || fail "codesign verify failed"

log "install /Applications/SeenShot.app"
rm -rf /Applications/SeenShot.app
ditto "${APP}" /Applications/SeenShot.app
/usr/bin/codesign --force --sign "${IDENTITY}" --options runtime --timestamp \
  --entitlements "${ENTITLEMENTS}" /Applications/SeenShot.app

log "launch"
open /Applications/SeenShot.app
sleep 1
pgrep -lf '/Applications/SeenShot.app/Contents/MacOS/SeenShot' || fail "SeenShot did not start from /Applications"
log "ok"
