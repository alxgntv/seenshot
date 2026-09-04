#!/bin/zsh
set -euo pipefail

# ─── Ariadne's Thread [AT-0389] ─────────────────────
# What: Build, macdeployqt, optional codesign and notarize DMG for SEENSHOT_ARCH
# Why:  arm64 and x86_64 Mac packages share one script
# Date: 2026-09-03
# Related: [AT-0389] packaging/macos/package_release.sh, CMakeLists.txt
# ─────────────────────────────────────────────────────

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SEENSHOT_ARCH="${SEENSHOT_ARCH:-arm64}"
BUILD="${ROOT}/build-${SEENSHOT_ARCH}"
APP="${BUILD}/SeenShot.app"
QT_PREFIX="${QT_PREFIX:-$(brew --prefix qtbase)}"
if [[ "${SEENSHOT_ARCH}" == "x86_64" && -d /usr/local/opt/qtbase ]]; then
  QT_PREFIX="/usr/local/opt/qtbase"
fi
MACDEPLOYQT="$(brew --prefix qttools)/bin/macdeployqt"
IDENTITY="${CODESIGN_IDENTITY:-}"
PROFILE="${NOTARY_PROFILE:-}"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
SDKROOT="${SDKROOT:-$DEVELOPER_DIR/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk}"
CXX="$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++"

echo "package_dmg: arch=${SEENSHOT_ARCH} root=${ROOT}"
cmake -S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="${QT_PREFIX}" \
  -DCMAKE_OSX_SYSROOT="${SDKROOT}" \
  -DCMAKE_CXX_COMPILER="${CXX}" \
  -DCMAKE_OBJCXX_COMPILER="${CXX}" \
  -DSEENSHOT_MAC_ARCH="${SEENSHOT_ARCH}"
cmake --build "${BUILD}" --config Release -j"$(sysctl -n hw.ncpu)"

if [[ ! -d "${APP}" ]]; then
  echo "package_dmg: app bundle missing ${APP}" >&2
  exit 1
fi

"${MACDEPLOYQT}" "${APP}" -always-overwrite
echo "package_dmg: macdeployqt done"

if [[ -n "${IDENTITY}" ]]; then
  ENTITLEMENTS="${ROOT}/packaging/macos/SeenShot.entitlements"
  echo "package_dmg: codesign identity=${IDENTITY}"
  codesign --force --deep --options runtime --entitlements "${ENTITLEMENTS}" --sign "${IDENTITY}" "${APP}"
  codesign --verify --deep --strict "${APP}"
else
  echo "package_dmg: CODESIGN_IDENTITY empty, skipping sign"
fi

VERSION="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "${ROOT}/packaging/macos/Info.plist")"
DMG="${BUILD}/SeenShot-${VERSION}-${SEENSHOT_ARCH}.dmg"
STAGE="${BUILD}/dmg-stage"
rm -rf "${STAGE}" "${DMG}"
mkdir -p "${STAGE}"
ditto "${APP}" "${STAGE}/SeenShot.app"
ln -s /Applications "${STAGE}/Applications"
hdiutil create -volname SeenShot -srcfolder "${STAGE}" -ov -format UDZO "${DMG}"
rm -rf "${STAGE}"
echo "package_dmg: wrote ${DMG} version=${VERSION}"

if [[ -n "${IDENTITY}" && -n "${PROFILE}" ]]; then
  echo "package_dmg: notarize profile=${PROFILE}"
  xcrun notarytool submit "${DMG}" --keychain-profile "${PROFILE}" --wait
  xcrun stapler staple "${DMG}"
  echo "package_dmg: notarized"
else
  echo "package_dmg: NOTARY_PROFILE empty, skipping notarize"
fi
