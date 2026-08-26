#!/bin/zsh
set -euo pipefail

# ─── Ariadne's Thread [AT-0027] ─────────────────────
# What: Build, macdeployqt, optional codesign and notarize arm64 DMG
# Why:  One-click Mac package from the plan
# Date: 2026-08-25
# Related: CMakeLists.txt
# ─────────────────────────────────────────────────────

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${ROOT}/build"
APP="${BUILD}/SeenShot.app"
QT_PREFIX="$(brew --prefix qtbase)"
MACDEPLOYQT="$(brew --prefix qttools)/bin/macdeployqt"
IDENTITY="${CODESIGN_IDENTITY:-}"
PROFILE="${NOTARY_PROFILE:-}"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
SDKROOT="${SDKROOT:-$DEVELOPER_DIR/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk}"
CXX="$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++"

echo "package_dmg: root=${ROOT}"
cmake -S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="${QT_PREFIX}" \
  -DCMAKE_OSX_SYSROOT="${SDKROOT}" \
  -DCMAKE_CXX_COMPILER="${CXX}" \
  -DCMAKE_OBJCXX_COMPILER="${CXX}"
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
DMG="${BUILD}/SeenShot-${VERSION}-arm64.dmg"
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
