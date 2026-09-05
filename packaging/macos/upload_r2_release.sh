#!/bin/zsh
set -euo pipefail

# ─── Ariadne's Thread [AT-0521] ─────────────────────
# What: Put notarized SeenShot DMGs into R2 prefix releases/
# Why:  Website /download must stream from project R2; GitHub stays fallback
# Date: 2026-09-05
# Related: [AT-0522] seenshot-web→lib/site.ts:serveLatestMacDmg, [AT-0421] packaging/macos/package_release.sh, https://developers.cloudflare.com/workers/wrangler/commands/#r2-object-put
# ─────────────────────────────────────────────────────

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
INFO_PLIST="${ROOT}/packaging/macos/Info.plist"
R2_BUCKET="${R2_BUCKET:-seenshot}"
R2_PREFIX="${R2_PREFIX:-releases}"
GITHUB_REPOSITORY="${GITHUB_REPOSITORY:-alxgntv/seenshot}"
FROM_GITHUB=0
DMG_ARM64=""
DMG_X86=""

log() {
  echo "upload_r2_release: $*"
}

fail() {
  echo "upload_r2_release: ERROR $*" >&2
  exit 1
}

VERSION="${VERSION:-$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "${INFO_PLIST}")}"
[[ -n "${VERSION}" ]] || fail "empty version"

usage() {
  fail "usage: upload_r2_release.sh [--from-github] [arm64.dmg x86_64.dmg]"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --from-github)
      FROM_GITHUB=1
      shift
      ;;
    -h|--help)
      usage
      ;;
    *)
      if [[ -z "${DMG_ARM64}" ]]; then
        DMG_ARM64="$1"
      elif [[ -z "${DMG_X86}" ]]; then
        DMG_X86="$1"
      else
        usage
      fi
      shift
      ;;
  esac
done

WEB="${SEENSHOT_WEB:-}"
if [[ -z "${WEB}" && -f "${ROOT}/../seenshot-web/wrangler.jsonc" ]]; then
  WEB="$(cd "${ROOT}/../seenshot-web" && pwd)"
fi
log "version=${VERSION} bucket=${R2_BUCKET} prefix=${R2_PREFIX} from_github=${FROM_GITHUB} web=${WEB}"

wrangler_put() {
  local object_path="$1"
  local file="$2"
  local filename="$3"
  local bytes
  bytes="$(stat -f%z "${file}")"
  log "put object=${object_path} file=${file} bytes=${bytes} name=${filename}"
  local attempt=1
  local max=4
  local -a cmd
  if [[ -n "${WEB}" && -f "${WEB}/package.json" ]]; then
    cmd=(npx wrangler r2 object put "${object_path}" --file "${file}" --remote --force \
      --content-type "application/octet-stream" \
      --cache-control "public, max-age=300")
  else
    cmd=(npx --yes wrangler@4 r2 object put "${object_path}" --file "${file}" --remote --force \
      --content-type "application/octet-stream" \
      --cache-control "public, max-age=300")
  fi
  log "wrangler ${cmd[*]}"
  while true; do
    log "put attempt=${attempt}/${max} object=${object_path}"
    if [[ -n "${WEB}" && -f "${WEB}/package.json" ]]; then
      if (cd "${WEB}" && "${cmd[@]}"); then
        log "put ok object=${object_path} bytes=${bytes} attempt=${attempt}"
        return 0
      fi
    else
      if "${cmd[@]}"; then
        log "put ok object=${object_path} bytes=${bytes} attempt=${attempt}"
        return 0
      fi
    fi
    if [[ "${attempt}" -ge "${max}" ]]; then
      fail "put failed object=${object_path} attempts=${attempt}"
    fi
    log "put retry object=${object_path} attempt=${attempt}"
    attempt=$((attempt + 1))
    sleep $((attempt * 2))
  done
}

put_arch() {
  local arch="$1"
  local file="$2"
  [[ -f "${file}" ]] || fail "dmg missing arch=${arch} file=${file}"
  local filename="SeenShot-${VERSION}-${arch}.dmg"
  local versioned="${R2_BUCKET}/${R2_PREFIX}/${filename}"
  local latest="${R2_BUCKET}/${R2_PREFIX}/latest-${arch}.dmg"
  wrangler_put "${versioned}" "${file}" "${filename}"
  wrangler_put "${latest}" "${file}" "${filename}"
  log "stored arch=${arch} versioned=${versioned} latest=${latest}"
}

write_latest_json() {
  local json
  json="$(mktemp)"
  python3 - "${json}" "${VERSION}" <<'PY'
import json
import sys
path, version = sys.argv[1], sys.argv[2]
payload = {
    "version": version,
    "arm64": {
        "key": "releases/latest-arm64.dmg",
        "name": "SeenShot-" + version + "-arm64.dmg",
    },
    "x86_64": {
        "key": "releases/latest-x86_64.dmg",
        "name": "SeenShot-" + version + "-x86_64.dmg",
    },
}
with open(path, "w", encoding="utf-8") as fh:
    json.dump(payload, fh)
    fh.write("\n")
print("upload_r2_release: latest.json version=" + version)
PY
  wrangler_put "${R2_BUCKET}/${R2_PREFIX}/latest.json" "${json}" "latest.json"
  rm -f "${json}"
}

fetch_github_latest() {
  local tmp
  tmp="$(mktemp -d)"
  local api="https://api.github.com/repos/${GITHUB_REPOSITORY}/releases/latest"
  local json="${tmp}/latest.json"
  log "github GET ${api}"
  curl -fsSL \
    -H "Accept: application/vnd.github+json" \
    -H "X-GitHub-Api-Version: 2022-11-28" \
    -H "User-Agent: SeenShot (https://seenshot.app)" \
    -o "${json}" \
    "${api}"
  python3 - "${json}" "${tmp}" <<'PY'
import json
import sys
path, out = sys.argv[1], sys.argv[2]
with open(path, encoding="utf-8") as fh:
    data = json.load(fh)
tag = str(data.get("tag_name") or "")
version = tag[1:] if tag.startswith("v") else tag
if not version:
    raise SystemExit("upload_r2_release: ERROR github latest has empty tag")
print("upload_r2_release: github tag=" + tag + " version=" + version)
found = {"arm64": "", "x86_64": ""}
for asset in data.get("assets") or []:
    name = str(asset.get("name") or "")
    url = str(asset.get("browser_download_url") or "")
    if ".dmg" not in name.lower() or not url:
        continue
    lower = name.lower()
    if "x86_64" in lower or "x86" in lower:
        found["x86_64"] = url + "\t" + name
        print("upload_r2_release: github asset arch=x86_64 name=" + name)
    elif "arm64" in lower or "aarch64" in lower:
        found["arm64"] = url + "\t" + name
        print("upload_r2_release: github asset arch=arm64 name=" + name)
missing = [arch for arch, value in found.items() if not value]
if missing:
    raise SystemExit("upload_r2_release: ERROR github missing dmg arch=" + ",".join(missing))
with open(out + "/version", "w", encoding="utf-8") as fh:
    fh.write(version + "\n")
for arch, value in found.items():
    url, name = value.split("\t", 1)
    dest = out + "/" + arch + ".url"
    with open(dest, "w", encoding="utf-8") as fh:
        fh.write(url + "\n" + name + "\n")
PY
  VERSION="$(tr -d '[:space:]' < "${tmp}/version")"
  [[ -n "${VERSION}" ]] || fail "github version empty"
  log "github version=${VERSION}"
  local arm_url x86_url arm_name x86_name
  arm_url="$(sed -n '1p' "${tmp}/arm64.url")"
  arm_name="$(sed -n '2p' "${tmp}/arm64.url")"
  x86_url="$(sed -n '1p' "${tmp}/x86_64.url")"
  x86_name="$(sed -n '2p' "${tmp}/x86_64.url")"
  DMG_ARM64="${tmp}/${arm_name}"
  DMG_X86="${tmp}/${x86_name}"
  log "curl arm64 url=${arm_url} dest=${DMG_ARM64}"
  curl -fL --retry 3 --retry-delay 2 -o "${DMG_ARM64}" "${arm_url}"
  log "curl x86_64 url=${x86_url} dest=${DMG_X86}"
  curl -fL --retry 3 --retry-delay 2 -o "${DMG_X86}" "${x86_url}"
  log "github files arm64=$(stat -f%z "${DMG_ARM64}") x86_64=$(stat -f%z "${DMG_X86}")"
}

if [[ "${FROM_GITHUB}" == "1" ]]; then
  fetch_github_latest
fi

if [[ -z "${DMG_ARM64}" ]]; then
  DMG_ARM64="${ROOT}/build-arm64/SeenShot-${VERSION}-arm64.dmg"
fi
if [[ -z "${DMG_X86}" ]]; then
  DMG_X86="${ROOT}/build-x86_64/SeenShot-${VERSION}-x86_64.dmg"
fi

log "arm64=${DMG_ARM64} x86_64=${DMG_X86}"
put_arch "arm64" "${DMG_ARM64}"
put_arch "x86_64" "${DMG_X86}"
write_latest_json
log "done version=${VERSION} prefix=${R2_PREFIX}"
