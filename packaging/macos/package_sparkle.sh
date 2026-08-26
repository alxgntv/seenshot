#!/bin/zsh
set -euo pipefail

# ─── Ariadne's Thread [AT-0120] ─────────────────────
# What: Build, Developer ID sign, notarize, zip, Sparkle appcast
# Why:  PRD-05 feed is zip + EdDSA on a stable HTTPS URL, not git
# Date: 2026-08-26
# Related: [AT-0027] packaging/macos/package_dmg.sh, [AT-0121] .github/workflows/release.yml
# ─────────────────────────────────────────────────────

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${ROOT}/build"
APP="${BUILD}/SeenShot.app"
ENTITLEMENTS="${ROOT}/packaging/macos/SeenShot.entitlements"
INFO_PLIST="${ROOT}/packaging/macos/Info.plist"
DOCS_APPCAST="${ROOT}/docs/appcast.xml"
PACK_APPCAST="${ROOT}/packaging/macos/appcast.xml"
QT_PREFIX="${QT_PREFIX:-$(brew --prefix qtbase)}"
MACDEPLOYQT="${MACDEPLOYQT:-$(brew --prefix qtbase)/bin/macdeployqt}"
if [[ ! -x "${MACDEPLOYQT}" ]]; then
  MACDEPLOYQT="$(brew --prefix qttools)/bin/macdeployqt"
fi
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
SDKROOT="${SDKROOT:-$DEVELOPER_DIR/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk}"
CXX="${CXX:-$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++}"
GITHUB_REPOSITORY="${GITHUB_REPOSITORY:-alxgntv/seenshot}"
SPARKLE_ACCOUNT="${SPARKLE_ACCOUNT:-seenshot}"
SPARKLE_ED_KEY_FILE="${SPARKLE_ED_KEY_FILE:-$HOME/.seenshot/eddsa_priv.pem}"
SPARKLE_BIN="${SPARKLE_BIN:-}"
ALLOW_DEVELOPMENT_SIGN="${ALLOW_DEVELOPMENT_SIGN:-0}"
SKIP_BUILD="${SKIP_BUILD:-0}"
SKIP_NOTARY="${SKIP_NOTARY:-0}"
SKIP_APPCAST="${SKIP_APPCAST:-0}"

log() {
  echo "package_sparkle: $*"
}

fail() {
  echo "package_sparkle: ERROR $*" >&2
  exit 1
}

plist_version() {
  /usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "${INFO_PLIST}"
}

VERSION="${VERSION:-$(plist_version)}"
[[ -n "${VERSION}" ]] || fail "empty version"
log "version=${VERSION} root=${ROOT} repo=${GITHUB_REPOSITORY}"

if [[ -z "${SPARKLE_BIN}" ]]; then
  if [[ -x /tmp/sparkle281/bin/sign_update ]]; then
    SPARKLE_BIN=/tmp/sparkle281/bin
  else
    fail "SPARKLE_BIN is empty and /tmp/sparkle281/bin/sign_update is missing"
  fi
fi
[[ -x "${SPARKLE_BIN}/sign_update" ]] || fail "sign_update missing in ${SPARKLE_BIN}"
[[ -x "${SPARKLE_BIN}/generate_appcast" ]] || fail "generate_appcast missing in ${SPARKLE_BIN}"

IDENTITY="${CODESIGN_IDENTITY:-}"
if [[ -z "${IDENTITY}" ]]; then
  IDENTITY="$(security find-identity -v -p codesigning | awk -F'\"' '/Developer ID Application/{print $2; exit}')"
fi
if [[ -z "${IDENTITY}" ]]; then
  fail "no Developer ID Application identity. Set CODESIGN_IDENTITY. Apple Development cannot ship."
fi
if [[ "${IDENTITY}" != *"Developer ID Application"* ]] && [[ "${ALLOW_DEVELOPMENT_SIGN}" != "1" ]]; then
  fail "identity is not Developer ID Application: ${IDENTITY}. Set ALLOW_DEVELOPMENT_SIGN=1 only for local tests."
fi
log "codesign identity=${IDENTITY}"

if [[ "${SKIP_BUILD}" != "1" ]]; then
  log "cmake configure"
  cmake -S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="${QT_PREFIX}" \
    -DCMAKE_OSX_SYSROOT="${SDKROOT}" \
    -DCMAKE_CXX_COMPILER="${CXX}" \
    -DCMAKE_OBJCXX_COMPILER="${CXX}"
  log "cmake build"
  cmake --build "${BUILD}" --config Release -j"$(sysctl -n hw.ncpu)"
fi

[[ -d "${APP}" ]] || fail "app bundle missing ${APP}"

if [[ ! -x "${MACDEPLOYQT}" ]]; then
  fail "macdeployqt missing"
fi
log "macdeployqt"
"${MACDEPLOYQT}" "${APP}" -always-overwrite
if [[ ! -d "${APP}/Contents/Frameworks/Sparkle.framework" ]]; then
  log "copy Sparkle.framework"
  mkdir -p "${APP}/Contents/Frameworks"
  cp -R "${ROOT}/third_party/Sparkle/Sparkle.framework" "${APP}/Contents/Frameworks/"
fi
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
  find "${APP}/Contents/Frameworks" -name '*.xpc' -print | while read -r xpc; do
    sign_nested "${xpc}"
  done
  find "${APP}/Contents/Frameworks" -name '*.framework' -maxdepth 1 -print | while read -r fw; do
    sign_nested "${fw}"
  done
fi
log "codesign app"
/usr/bin/codesign --force --sign "${IDENTITY}" --options runtime --timestamp \
  --entitlements "${ENTITLEMENTS}" "${APP}"
/usr/bin/codesign --verify --deep --strict "${APP}"
log "codesign verify ok"

ZIP_NAME="SeenShot-${VERSION}-arm64.zip"
ZIP_PATH="${BUILD}/${ZIP_NAME}"
rm -f "${ZIP_PATH}"
log "zip ${ZIP_NAME}"
ditto -c -k --keepParent "${APP}" "${ZIP_PATH}"
[[ -f "${ZIP_PATH}" ]] || fail "zip missing"

if [[ "${SKIP_NOTARY}" != "1" ]]; then
  if [[ -n "${NOTARY_KEY:-}" && -n "${NOTARY_KEY_ID:-}" && -n "${NOTARY_ISSUER:-}" ]]; then
    KEY_FILE="$(mktemp)"
    printf '%s' "${NOTARY_KEY}" > "${KEY_FILE}"
    log "notarytool submit api key"
    xcrun notarytool submit "${ZIP_PATH}" --key "${KEY_FILE}" --key-id "${NOTARY_KEY_ID}" \
      --issuer "${NOTARY_ISSUER}" --wait
    rm -f "${KEY_FILE}"
  elif [[ -n "${NOTARY_PROFILE:-}" ]]; then
    log "notarytool submit profile=${NOTARY_PROFILE}"
    xcrun notarytool submit "${ZIP_PATH}" --keychain-profile "${NOTARY_PROFILE}" --wait
  else
    fail "notary credentials missing. Set NOTARY_KEY+NOTARY_KEY_ID+NOTARY_ISSUER or NOTARY_PROFILE, or SKIP_NOTARY=1"
  fi
  log "stapler staple app"
  xcrun stapler staple "${APP}"
  rm -f "${ZIP_PATH}"
  ditto -c -k --keepParent "${APP}" "${ZIP_PATH}"
  log "re-zip after staple"
else
  log "skip notary"
fi

LENGTH="$(stat -f%z "${ZIP_PATH}")"
log "zip length=${LENGTH}"

SIGN_ARGS=(--ed-key-file "${SPARKLE_ED_KEY_FILE}")
if [[ ! -f "${SPARKLE_ED_KEY_FILE}" ]]; then
  if [[ -n "${SPARKLE_ED_PRIVATE_KEY:-}" ]]; then
    SPARKLE_ED_KEY_FILE="$(mktemp)"
    printf '%s' "${SPARKLE_ED_PRIVATE_KEY}" > "${SPARKLE_ED_KEY_FILE}"
    SIGN_ARGS=(--ed-key-file "${SPARKLE_ED_KEY_FILE}")
    log "using SPARKLE_ED_PRIVATE_KEY from env"
  else
    SIGN_ARGS=(--account "${SPARKLE_ACCOUNT}")
    log "using keychain account=${SPARKLE_ACCOUNT}"
  fi
else
  log "using ed key file"
fi

ED_SIG="$("${SPARKLE_BIN}/sign_update" -p "${SIGN_ARGS[@]}" "${ZIP_PATH}")"
[[ -n "${ED_SIG}" ]] || fail "sign_update produced empty signature"
log "edSignature chars=${#ED_SIG}"

if [[ "${SKIP_APPCAST}" != "1" ]]; then
  ARCHIVES="$(mktemp -d)"
  cp "${ZIP_PATH}" "${ARCHIVES}/${ZIP_NAME}"
  if [[ -f "${DOCS_APPCAST}" ]]; then
    cp "${DOCS_APPCAST}" "${ARCHIVES}/appcast.xml"
  fi
  PREFIX="https://github.com/${GITHUB_REPOSITORY}/releases/download/v${VERSION}/"
  log "generate_appcast prefix=${PREFIX}"
  APPCAST_KEY_ARGS=("${SIGN_ARGS[@]}")
  "${SPARKLE_BIN}/generate_appcast" \
    "${APPCAST_KEY_ARGS[@]}" \
    --download-url-prefix "${PREFIX}" \
    --maximum-deltas 0 \
    --maximum-versions 20 \
    -o "${ARCHIVES}/appcast.xml" \
    "${ARCHIVES}"
  mkdir -p "${ROOT}/docs"
  cp "${ARCHIVES}/appcast.xml" "${DOCS_APPCAST}"
  cp "${ARCHIVES}/appcast.xml" "${PACK_APPCAST}"
  log "wrote ${DOCS_APPCAST}"
  rm -rf "${ARCHIVES}"
fi

if [[ "${SPARKLE_ED_KEY_FILE}" == /tmp/* ]] || [[ "${SPARKLE_ED_KEY_FILE}" == /var/folders/* ]]; then
  rm -f "${SPARKLE_ED_KEY_FILE}"
fi

echo "${ZIP_PATH}"
log "done zip=${ZIP_PATH} length=${LENGTH}"
