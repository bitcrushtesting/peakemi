#!/bin/bash
# Bundle a built PeakEmi.app into a distributable disk image.
#
#     scripts/macos/make-dmg.sh --app build/release/bin/peakemi.app \
#                               --output PeakEmi-0.1.0-macos-arm64.dmg \
#                               --version 0.1.0 \
#                               [--qt-bin /path/to/Qt/bin] [--skip-deploy]
#                               [--identity "Developer ID Application: ..."]
#
# The image contains the application, a symlink to /Applications so the user can
# drag one onto the other, and the licence the binaries are distributed under.
set -euo pipefail

app_bundle=""
output_dmg=""
version=""
qt_bin=""
skip_deploy=0
identity="-"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --app)        app_bundle="$2"; shift 2 ;;
        --output)     output_dmg="$2"; shift 2 ;;
        --version)    version="$2"; shift 2 ;;
        --qt-bin)     qt_bin="$2"; shift 2 ;;
        --skip-deploy) skip_deploy=1; shift ;;
        --identity)   identity="$2"; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

[[ -n "${app_bundle}" ]] || { echo "--app is required" >&2; exit 2; }
[[ -n "${output_dmg}" ]] || { echo "--output is required" >&2; exit 2; }
[[ -d "${app_bundle}" ]] || { echo "no application bundle at ${app_bundle}" >&2; exit 1; }

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "make-dmg.sh only runs on macOS" >&2
    exit 1
fi

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
version="${version:-0.0.0}"

# --- 1. Bring Qt into the bundle -------------------------------------------
# macdeployqt copies the frameworks and plugins the binary references and
# rewrites its load paths, which is what makes the bundle work on a machine
# with no Qt installed.
if [[ "${skip_deploy}" -eq 0 ]]; then
    macdeployqt="${qt_bin:+${qt_bin}/}macdeployqt"
    if ! command -v "${macdeployqt}" >/dev/null 2>&1 && [[ ! -x "${macdeployqt}" ]]; then
        echo "macdeployqt not found; pass --qt-bin <Qt>/bin or --skip-deploy" >&2
        exit 1
    fi

    # macdeployqt only brings the plugins the application asks for, which on
    # macOS means cocoa alone. The offscreen platform goes in as well: it is
    # what lets the shipped application run without a window server, for the
    # screenshot tool, for scripted runs and for the launch check below.
    extra_executables=()
    plugins_dir="${qt_bin:+${qt_bin}/../plugins}"
    offscreen="${plugins_dir}/platforms/libqoffscreen.dylib"
    if [[ -n "${qt_bin}" && -f "${offscreen}" ]]; then
        mkdir -p "${app_bundle}/Contents/PlugIns/platforms"
        cp "${offscreen}" "${app_bundle}/Contents/PlugIns/platforms/"
        # Named explicitly so macdeployqt rewrites this plugin's load paths too,
        # instead of leaving it pointing at the Qt on the build machine.
        extra_executables+=("-executable=${app_bundle}/Contents/PlugIns/platforms/libqoffscreen.dylib")
    fi

    echo "==> deploying Qt into $(basename "${app_bundle}")"
    "${macdeployqt}" "${app_bundle}" -always-overwrite "${extra_executables[@]}"
fi

# --- 1b. Sign the bundle ---------------------------------------------------
# Rewriting load commands invalidates the signatures the binaries came with,
# and macOS refuses to run a binary whose signature does not match its
# contents -- on Apple Silicon it kills the process outright. Signing is
# therefore part of assembling the bundle, not an optional extra.
#
# Without --identity this is an ad-hoc signature: enough to run, not enough to
# clear Gatekeeper on another machine. Notarisation with a real Developer ID is
# milestone M8 and needs credentials this script does not have.
echo "==> signing the bundle (identity: ${identity})"
while IFS= read -r binary; do
    codesign --force --timestamp=none --sign "${identity}" "${binary}" >/dev/null 2>&1 || {
        echo "warning: could not sign ${binary}" >&2
    }
done < <(find "${app_bundle}/Contents/Frameworks" "${app_bundle}/Contents/PlugIns" \
             -type f \( -name '*.dylib' -o -perm -u+x \) 2>/dev/null || true)

# The bundle itself is signed last: its seal covers everything nested inside.
codesign --force --timestamp=none --sign "${identity}" "${app_bundle}"
codesign --verify --strict "${app_bundle}"

# A bundle that still points at a Qt outside itself would launch here and fail
# on any other machine, which is exactly the bug this check exists to catch.
if otool -L "${app_bundle}/Contents/MacOS/peakemi" | grep -qE '^\s+/(Users|opt|usr/local)/.*Qt'; then
    echo "error: the bundle still references a Qt outside itself:" >&2
    otool -L "${app_bundle}/Contents/MacOS/peakemi" | grep -E 'Qt' >&2
    exit 1
fi

# --- 2. Stage what the image should contain --------------------------------
staging="$(mktemp -d)"
trap 'rm -rf "${staging}"; [[ -n "${mounted:-}" ]] && hdiutil detach "${mounted}" -quiet || true' EXIT

echo "==> staging the disk image contents"
cp -R "${app_bundle}" "${staging}/PeakEmi.app"
ln -s /Applications "${staging}/Applications"
cp "${repository_root}/LICENSE" "${staging}/LICENSE.txt"
cp "${repository_root}/README.md" "${staging}/README.md"

# The volume icon shows up in Finder's sidebar and on the desktop.
if [[ -f "${app_bundle}/Contents/Resources/peakemi.icns" ]]; then
    cp "${app_bundle}/Contents/Resources/peakemi.icns" "${staging}/.VolumeIcon.icns"
    # SetFile ships with the Xcode command line tools; without it the image is
    # still valid, it just uses the default volume icon.
    if command -v SetFile >/dev/null 2>&1; then
        SetFile -a C "${staging}"
    fi
fi

# --- 3. Build the image ----------------------------------------------------
volume_name="PeakEmi ${version}"
mkdir -p "$(dirname "${output_dmg}")"
rm -f "${output_dmg}"

echo "==> creating ${output_dmg}"
hdiutil create \
    -volname "${volume_name}" \
    -srcfolder "${staging}" \
    -fs HFS+ \
    -format UDZO \
    -imagekey zlib-level=9 \
    -ov -quiet \
    "${output_dmg}"

# --- 4. Prove the image is usable ------------------------------------------
# An image that cannot be mounted, or whose application will not start, is not
# worth shipping; both are cheap to check here and expensive to discover later.
echo "==> verifying ${output_dmg}"
hdiutil verify "${output_dmg}" -quiet
mounted="$(hdiutil attach "${output_dmg}" -nobrowse -readonly | grep -Eo '/Volumes/.*$' | head -1)"
[[ -d "${mounted}/PeakEmi.app" ]] || { echo "the image contains no PeakEmi.app" >&2; exit 1; }
[[ -L "${mounted}/Applications" ]] || { echo "the image has no Applications symlink" >&2; exit 1; }

echo "    mounted at ${mounted}"
launch_log="$(QT_QPA_PLATFORM=offscreen "${mounted}/PeakEmi.app/Contents/MacOS/peakemi" --version 2>&1 || true)"
reported="$(printf '%s\n' "${launch_log}" | grep -E '^PeakEmi ' | tail -1)"
if [[ -z "${reported}" ]]; then
    echo "the application in the image did not start:" >&2
    printf '%s\n' "${launch_log}" >&2
    exit 1
fi
echo "    application reports: ${reported}"

hdiutil detach "${mounted}" -quiet
mounted=""

size="$(du -h "${output_dmg}" | cut -f1 | tr -d ' ')"
echo "==> ${output_dmg} (${size})"
