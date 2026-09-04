#!/bin/zsh
set -euo pipefail

# ─── Ariadne's Thread [AT-0389] ─────────────────────
# What: Build arm64 + x86_64 DMGs, notarize both, publish one Sparkle appcast
# Why:  Intel MacBooks need x86_64; Apple Silicon keeps arm64; Sparkle picks the right enclosure
# Date: 2026-09-03
# Related: [AT-0389] packaging/macos/package_sparkle.sh, [AT-0121] .github/workflows/release.yml
# ─────────────────────────────────────────────────────

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
INFO_PLIST="${ROOT}/packaging/macos/Info.plist"
DOCS_APPCAST="${ROOT}/docs/appcast.xml"
PACK_APPCAST="${ROOT}/packaging/macos/appcast.xml"
GITHUB_REPOSITORY="${GITHUB_REPOSITORY:-alxgntv/seenshot}"
SPARKLE_BIN="${SPARKLE_BIN:-}"
SPARKLE_ACCOUNT="${SPARKLE_ACCOUNT:-seenshot}"
SPARKLE_ED_KEY_FILE="${SPARKLE_ED_KEY_FILE:-$HOME/.seenshot/eddsa_priv.pem}"

log() {
  echo "package_release: $*"
}

fail() {
  echo "package_release: ERROR $*" >&2
  exit 1
}

VERSION="${VERSION:-$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "${INFO_PLIST}")}"
[[ -n "${VERSION}" ]] || fail "empty version"
log "version=${VERSION} root=${ROOT}"

ensure_x86_qt() {
  if [[ -n "${QT_PREFIX_X86_64:-}" && -d "${QT_PREFIX_X86_64}" ]]; then
    log "x86_64 qt from QT_PREFIX_X86_64=${QT_PREFIX_X86_64}"
    return 0
  fi
  if [[ -d /usr/local/opt/qtbase ]]; then
    log "x86_64 qt already at /usr/local/opt/qtbase"
    return 0
  fi
  log "install Rosetta Homebrew + qt for x86_64"
  if [[ ! -x /usr/local/bin/brew ]]; then
    NONINTERACTIVE=1 arch -x86_64 /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
  fi
  arch -x86_64 /usr/local/bin/brew install qt curl
  [[ -d /usr/local/opt/qtbase ]] || fail "x86_64 qt missing after brew install"
}

build_arch() {
  local arch="$1"
  local qt_prefix="$2"
  log "build arch=${arch} qt=${qt_prefix}"
  SEENSHOT_ARCH="${arch}" \
    QT_PREFIX="${qt_prefix}" \
    SKIP_APPCAST=1 \
    VERSION="${VERSION}" \
    NOTARY_PROFILE="${NOTARY_PROFILE:-}" \
    "${ROOT}/packaging/macos/package_sparkle.sh"
}

build_arch arm64 "${QT_PREFIX_ARM64:-/opt/homebrew/opt/qtbase}"
ensure_x86_qt
build_arch x86_64 "${QT_PREFIX_X86_64:-/usr/local/opt/qtbase}"

DMG_ARM64="${ROOT}/build-arm64/SeenShot-${VERSION}-arm64.dmg"
DMG_X86="${ROOT}/build-x86_64/SeenShot-${VERSION}-x86_64.dmg"
[[ -f "${DMG_ARM64}" ]] || fail "arm64 dmg missing ${DMG_ARM64}"
[[ -f "${DMG_X86}" ]] || fail "x86_64 dmg missing ${DMG_X86}"
log "dmgs ready arm64=$(stat -f%z "${DMG_ARM64}") x86_64=$(stat -f%z "${DMG_X86}")"

if [[ -z "${SPARKLE_BIN}" ]]; then
  if [[ -x /tmp/sparkle281/bin/sign_update ]]; then
    SPARKLE_BIN=/tmp/sparkle281/bin
  else
    fail "SPARKLE_BIN is empty"
  fi
fi
[[ -x "${SPARKLE_BIN}/generate_appcast" ]] || fail "generate_appcast missing in ${SPARKLE_BIN}"

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

ARCHIVES="$(mktemp -d)"
cp "${DMG_ARM64}" "${ARCHIVES}/"
cp "${DMG_X86}" "${ARCHIVES}/"
if [[ -f "${DOCS_APPCAST}" ]]; then
  cp "${DOCS_APPCAST}" "${ARCHIVES}/appcast.xml"
fi
PREFIX="https://github.com/${GITHUB_REPOSITORY}/releases/download/v${VERSION}/"
log "generate_appcast prefix=${PREFIX} archives=$(ls -1 "${ARCHIVES}")"
"${SPARKLE_BIN}/generate_appcast" \
  "${SIGN_ARGS[@]}" \
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

if [[ "${SPARKLE_ED_KEY_FILE}" == /tmp/* ]] || [[ "${SPARKLE_ED_KEY_FILE}" == /var/folders/* ]]; then
  rm -f "${SPARKLE_ED_KEY_FILE}"
fi

echo "DMG_ARM64=${DMG_ARM64}"
echo "DMG_X86_64=${DMG_X86}"
log "done version=${VERSION}"
