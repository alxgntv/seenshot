#!/bin/zsh
set -euo pipefail

# ─── Ariadne's Thread [AT-0323] ─────────────────────
# What: pkgbuild SeenShot.app into a component package with postinstall
# Why:  macOS Installer must launch the app so first-run setup starts after install
# Date: 2026-08-28
# Related: [AT-0322] packaging/macos/pkg/scripts/postinstall, pkgbuild(1)
# ─────────────────────────────────────────────────────

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${ROOT}/build"
APP="${APP:-${BUILD}/SeenShot.app}"
INFO_PLIST="${ROOT}/packaging/macos/Info.plist"
SCRIPTS="${ROOT}/packaging/macos/pkg/scripts"

log() {
  echo "package_pkg: $*"
}

fail() {
  echo "package_pkg: ERROR $*" >&2
  exit 1
}

[[ -d "${APP}" ]] || fail "app missing ${APP}"
[[ -x "${SCRIPTS}/postinstall" ]] || fail "postinstall not executable ${SCRIPTS}/postinstall"

VERSION="${VERSION:-$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "${INFO_PLIST}")}"
[[ -n "${VERSION}" ]] || fail "empty version"
SEENSHOT_ARCH="${SEENSHOT_ARCH:-arm64}"

PKG_NAME="SeenShot-${VERSION}-${SEENSHOT_ARCH}.pkg"
PKG_PATH="${BUILD}/${PKG_NAME}"
PAYLOAD="$(mktemp -d)"
cleanup() {
  rm -rf "${PAYLOAD}"
}
trap cleanup EXIT

log "payload ${PAYLOAD}/SeenShot.app from ${APP}"
ditto "${APP}" "${PAYLOAD}/SeenShot.app"

log "pkgbuild identifier=com.seenshot.app.pkg version=${VERSION}"
pkgbuild \
  --root "${PAYLOAD}" \
  --identifier com.seenshot.app.pkg \
  --version "${VERSION}" \
  --install-location /Applications \
  --scripts "${SCRIPTS}" \
  "${PKG_PATH}"

[[ -f "${PKG_PATH}" ]] || fail "pkg missing ${PKG_PATH}"
log "wrote ${PKG_PATH} bytes=$(stat -f%z "${PKG_PATH}")"
echo "${PKG_PATH}"
