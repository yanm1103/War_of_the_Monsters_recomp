#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

fail() { printf 'AppImage build: %s\n' "$*" >&2; exit 1; }

[[ -n "${1:-}" ]] || fail "usage: $0 /absolute/path/WotM.AppImage (optionally set WOTM_FFMPEG_SOURCE_DIR)"
[[ "$1" = /* && "$1" = *.AppImage ]] || fail "output must be an absolute .AppImage path"

. /etc/os-release
[[ "${ID:-}" == ubuntu && "${VERSION_ID:-}" == 22.04 ]] || fail "the reproducible AppImage build environment is Ubuntu 22.04 x86-64; this restriction is for building, not running the AppImage"
[[ "$(uname -m)" == x86_64 ]] || fail "only x86-64 is supported"
[[ -f "$repo/recomp/retail/register_functions.cpp" ]] || fail "generated runtime inputs are missing; build locally from your own ISO first"
[[ -z "${WOTM_FFMPEG_PREFIX:-}" && -z "${WOTM_FFMPEG_SYSROOT:-}" && -z "${WOTM_EXE_LINKER_FLAGS:-}" ]] || fail "unset package override variables; the build pins FFmpeg and linker flags"

for tool in cmake ninja pkg-config cc gcc make python3 curl gpg sha256sum readelf patchelf file desktop-file-validate mksquashfs unsquashfs nasm; do
    command -v "$tool" >/dev/null || fail "missing prerequisite: $tool"
done

output=$(realpath -m -- "$1")
repo_real=$(realpath -e -- "$repo")
case "$output" in "$repo_real"/*) fail "output must be outside the repository";; esac
output_dir=$(dirname -- "$output")
mkdir -p "$output_dir"
[[ ! -e "$output" && ! -L "$output" ]] || fail "output already exists: $output"
stem=${output##*/}
stem=${stem%.AppImage}
source_out="$(dirname -- "$output")/$stem.ffmpeg-7.1.tar.xz"
[[ ! -e "$source_out" && ! -e "$source_out.asc" && ! -e "$source_out.sha256" ]] || fail "FFmpeg source sidecar already exists"

keep_work=0
if [[ -n "${WOTM_APPIMAGE_WORKDIR:-}" ]]; then
    work=$(realpath -m -- "$WOTM_APPIMAGE_WORKDIR")
    case "$work" in "$repo_real"|"$repo_real"/*) fail "work directory must be outside the repository";; esac
    [[ ! -e "$work" || -z "$(find "$work" -mindepth 1 -maxdepth 1 -print -quit)" ]] || fail "work directory must be empty: $work"
    mkdir -p "$work"
    keep_work=1
else
    work=$(mktemp -d "${TMPDIR:-/tmp}/wotm-appimage.XXXXXX")
fi
case "$output" in "$work"/*) fail "output must not be inside the temporary work directory";; esac
cleanup() {
    status=$?
    if [[ "$keep_work" == 0 && "$status" == 0 ]]; then
        rm -rf -- "$work"
    else
        printf 'AppImage build work files: %s\n' "$work" >&2
    fi
}
trap cleanup EXIT

ffmpeg_url=https://ffmpeg.org/releases/ffmpeg-7.1.tar.xz
ffmpeg_sig_url=https://ffmpeg.org/releases/ffmpeg-7.1.tar.xz.asc
ffmpeg_sha=40973d44970dbc83ef302b0609f2e74982be2d85916dd2ee7472d30678a7abe6
ffmpeg_key=FCF986EA15E6E293A5644F10B4322F04D67658D8
runtime_hash=617aa77314d57c8b8e4fae9c3f0b35af7b30d5529cb69e98f64f92510ed98a46
ffmpeg_flags='--prefix=/usr --libdir=/usr/lib --disable-static --enable-shared --disable-gpl --disable-version3 --disable-nonfree --disable-autodetect --disable-programs --disable-doc --disable-debug --disable-everything --enable-avcodec --enable-avformat --enable-avutil --enable-swresample --enable-swscale --enable-decoder=mpeg2video --enable-parser=mpegvideo --enable-protocol=file --enable-runtime-cpudetect'
archive="$work/ffmpeg-7.1.tar.xz"
signature="$archive.asc"
if [[ -n "${WOTM_FFMPEG_SOURCE_DIR:-}" ]]; then
    source_dir=$(realpath -e -- "$WOTM_FFMPEG_SOURCE_DIR")
    for source_file in ffmpeg-7.1.tar.xz ffmpeg-7.1.tar.xz.asc ffmpeg-devel.asc; do
        [[ -r "$source_dir/$source_file" ]] || fail "missing FFmpeg source input: $source_dir/$source_file"
    done
    cp -- "$source_dir/ffmpeg-7.1.tar.xz" "$archive"
    cp -- "$source_dir/ffmpeg-7.1.tar.xz.asc" "$signature"
    cp -- "$source_dir/ffmpeg-devel.asc" "$work/ffmpeg-devel.asc"
else
    curl --fail --location --retry 3 "$ffmpeg_url" -o "$archive"
    curl --fail --location --retry 3 "$ffmpeg_sig_url" -o "$signature"
fi
printf '%s  %s\n' "$ffmpeg_sha" "$archive" | sha256sum --check --status || fail "FFmpeg source SHA-256 mismatch"
mkdir -m 700 "$work/gnupg"
if [[ -z "${WOTM_FFMPEG_SOURCE_DIR:-}" ]]; then
    curl --fail --location --retry 3 https://ffmpeg.org/ffmpeg-devel.asc -o "$work/ffmpeg-devel.asc"
fi
gpg --homedir "$work/gnupg" --import "$work/ffmpeg-devel.asc" >/dev/null 2>&1
fingerprint=$(gpg --homedir "$work/gnupg" --with-colons --fingerprint | awk -F: '$1 == "fpr" { print $10; exit }')
[[ "$fingerprint" == "$ffmpeg_key" ]] || fail "unexpected FFmpeg signing-key fingerprint: $fingerprint"
gpg --homedir "$work/gnupg" --verify "$signature" "$archive" >/dev/null 2>&1 || fail "FFmpeg source signature verification failed"

ffmpeg_source="$work/ffmpeg-7.1"
ffmpeg_sysroot="$work/ffmpeg-root"
ffmpeg_prefix="$ffmpeg_sysroot/usr"
mkdir -p "$ffmpeg_source"
tar -xf "$archive" --no-same-owner --strip-components=1 -C "$ffmpeg_source"
(
    cd "$ffmpeg_source"
    ./configure --prefix=/usr --libdir=/usr/lib --disable-static --enable-shared \
        --disable-gpl --disable-version3 --disable-nonfree --disable-autodetect \
        --disable-programs --disable-doc --disable-debug --disable-everything \
        --enable-avcodec --enable-avformat --enable-avutil --enable-swresample \
        --enable-swscale --enable-decoder=mpeg2video --enable-parser=mpegvideo \
        --enable-protocol=file --enable-runtime-cpudetect > "$work/ffmpeg-configure.log"
    make -j"${JOBS:-$(nproc)}" > "$work/ffmpeg-build.log" 2>&1
    make install DESTDIR="$ffmpeg_sysroot" >> "$work/ffmpeg-build.log" 2>&1
)

ffmpeg_pc="$ffmpeg_prefix/lib/pkgconfig"
[[ -d "$ffmpeg_pc" ]] || fail "missing FFmpeg pkg-config directory: $ffmpeg_pc"
export PKG_CONFIG_PATH="$ffmpeg_pc"
export PKG_CONFIG_LIBDIR="$ffmpeg_pc"
export PKG_CONFIG_ALLOW_SYSTEM_CFLAGS=1
export PKG_CONFIG_ALLOW_SYSTEM_LIBS=1
if [[ -n "$ffmpeg_sysroot" ]]; then
    export PKG_CONFIG_SYSROOT_DIR="$ffmpeg_sysroot"
else
    unset PKG_CONFIG_SYSROOT_DIR || true
fi
ffmpeg_version=$(pkg-config --modversion libavcodec)
[[ "$ffmpeg_version" == 61.* ]] || fail "expected FFmpeg 7.1/libavcodec 61, got $ffmpeg_version"
cc "$repo/tools/appimage/ffmpeg_probe.c" $(pkg-config --cflags --libs libavcodec libavformat libavutil libswresample libswscale) -o "$work/ffmpeg-probe"
env LD_LIBRARY_PATH="$ffmpeg_prefix/lib" "$work/ffmpeg-probe" > "$work/ffmpeg-probe.log" 2>&1 || fail "LGPL/decoder probe failed; see $work/ffmpeg-probe.log"
grep -qF 'LGPL version 2.1 or later' "$work/ffmpeg-probe.log" || fail "FFmpeg libraries are not LGPL 2.1-or-later"
probe_config=$(tail -n 1 "$work/ffmpeg-probe.log")
[[ "$probe_config" == "$ffmpeg_flags" ]] || fail "FFmpeg configuration differs from the reviewed LGPL build: $probe_config"

build_dir=$(realpath -m -- "${WOTM_BUILD_DIR:-$work/build}")
case "$build_dir" in "$repo_real"|"$repo_real"/*) fail "runtime build directory must be outside the repository";; esac
export WOTM_BUILD_DIR="$build_dir"
export WOTM_FFMPEG_PREFIX="$ffmpeg_prefix"
export WOTM_EXE_LINKER_FLAGS='-static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL'
if [[ -n "$ffmpeg_sysroot" ]]; then export WOTM_FFMPEG_SYSROOT="$ffmpeg_sysroot"; else unset WOTM_FFMPEG_SYSROOT || true; fi
"$repo/tools/build_runtime.sh" 2>&1 | tee "$work/wotm-build.log"
exe="$build_dir/ps2xRuntime/wotm"
[[ -x "$exe" ]] || fail "runtime build did not produce $exe"
needed=$(readelf -d "$exe")
for soname in libavcodec.so.61 libavutil.so.59 libswscale.so.8; do
    grep -qF "$soname" <<<"$needed" || fail "runtime is not linked to expected FFmpeg soname $soname"
done
grep -qF "$ffmpeg_prefix/lib" <<<"$needed" || fail "runtime is not linked to the verified FFmpeg prefix"
if grep -Eq 'Shared library: \[lib(std(c\+\+)?|gcc_s)\.so' <<<"$needed"; then
    fail "packaged runtime has a dynamic GCC C++ runtime dependency"
fi

appdir="$work/AppDir"
mkdir -p "$appdir/usr/bin/assets" "$appdir/usr/share/doc/wotm"
install -m 755 "$repo/tools/release/linux/AppRun" "$appdir/AppRun"
install -m 644 "$repo/tools/release/linux/wotm.desktop" "$appdir/wotm.desktop"
install -m 644 "$repo/tools/release/linux/wotm.svg" "$appdir/wotm.svg"
install -m 755 "$exe" "$appdir/usr/bin/wotm"
for asset in Oswald-Medium.ttf Oswald_OFL.txt prompts.png prompts_LICENSE.txt; do
    install -m 644 "$repo/native/assets/$asset" "$appdir/usr/bin/assets/$asset"
done
for doc in README.txt THIRD_PARTY.txt FFmpeg-BUILD.txt AppImageKit-LICENSE.txt; do
    install -m 644 "$repo/tools/release/linux/$doc" "$appdir/usr/share/doc/wotm/$doc"
done
install -m 644 "$repo/LICENSE" "$appdir/usr/share/doc/wotm/LICENSE"
install -m 644 /usr/share/doc/libstdc++6/copyright "$appdir/usr/share/doc/wotm/GCC-Runtime-copyright.txt"
install -m 644 "$repo/runtime/PS2Recomp/LICENSE" "$appdir/usr/share/doc/wotm/PS2Recomp-LICENSE.md"
install -m 644 "$repo/tools/release/licenses/FFmpeg-LICENSE.md" "$repo/tools/release/licenses/LGPL-2.1.txt" "$appdir/usr/share/doc/wotm/"
raylib="$build_dir/_deps/raylib-src"
install -m 644 "$raylib/LICENSE" "$appdir/usr/share/doc/wotm/raylib-LICENSE.txt"
install -m 644 "$raylib/src/external/glfw/LICENSE.md" "$appdir/usr/share/doc/wotm/GLFW-LICENSE.md"
version=$(sed -n 's/^#define WOTM_VERSION "\(.*\)"/\1/p' "$build_dir/wotm_gen/wotm_version.gen.h")
[[ -n "$version" ]] || fail "could not read runtime version"
{
    printf 'WotM version: %s\nBuild baseline: Ubuntu 22.04 x86-64 (glibc 2.35)\nFFmpeg: LGPL-only n7.1 build; see FFmpeg-BUILD.txt\n' "$version"
    printf 'AppImage runtime: type2-runtime commit fedfad5 (SHA-256 %s)\n' "$runtime_hash"
    printf 'C++ runtime: statically linked with GCC Runtime Library Exception; see GCC-Runtime-copyright.txt\n'
    printf 'Compiler: %s\n' "$(gcc --version | head -n 1)"
    printf 'CMake: %s\n' "$(cmake --version | head -n 1)"
    printf 'Ninja: %s\n' "$(ninja --version)"
} > "$appdir/usr/share/doc/wotm/BUILD.txt"

linuxdeploy_hash=c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
appimagetool_hash=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
linuxdeploy_url=https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage
appimagetool_url=https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage
runtime_url=https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64
fetch_tool() {
    local env_name=$1 dest=$2 url=$3 hash=$4 value=${!1:-}
    if [[ -n "$value" ]]; then
        dest=$(realpath -e -- "$value")
        [[ -x "$dest" ]] || fail "$env_name is not executable: $dest"
    else
        curl --fail --location --retry 3 "$url" -o "$dest"
        chmod 755 "$dest"
    fi
    printf '%s  %s\n' "$hash" "$dest" | sha256sum --check --status || fail "$env_name SHA-256 mismatch"
    printf '%s\n' "$dest"
}
linuxdeploy=$(fetch_tool LINUXDEPLOY "$work/linuxdeploy.AppImage" "$linuxdeploy_url" "$linuxdeploy_hash")
appimagetool=$(fetch_tool APPIMAGETOOL "$work/appimagetool.AppImage" "$appimagetool_url" "$appimagetool_hash")
runtime=$(fetch_tool APPIMAGE_RUNTIME "$work/runtime-x86_64" "$runtime_url" "$runtime_hash")
python3 "$repo/tools/check_appimage.py" --check-elf "$runtime" || fail "pinned AppImage runtime exceeds the glibc 2.35 baseline"

excludes=(
    '--exclude-library=libX11.so*' '--exclude-library=libxcb.so*' '--exclude-library=libXau.so*'
    '--exclude-library=libXdmcp.so*' '--exclude-library=libbsd.so*' '--exclude-library=libmd.so*'
    '--exclude-library=libOpenGL.so*' '--exclude-library=libGL*.so*' '--exclude-library=libEGL.so*'
    '--exclude-library=libgbm.so*' '--exclude-library=libdrm.so*' '--exclude-library=libasound.so*'
    '--exclude-library=libpulse.so*' '--exclude-library=libc.so*' '--exclude-library=libm.so*'
    '--exclude-library=libpthread.so*' '--exclude-library=libdl.so*' '--exclude-library=ld-linux*.so*'
    '--exclude-library=libstdc++.so*' '--exclude-library=libgcc_s.so*'
)
APPIMAGE_EXTRACT_AND_RUN=1 LD_LIBRARY_PATH="$ffmpeg_prefix/lib" "$linuxdeploy" \
    --appdir "$appdir" --executable "$appdir/usr/bin/wotm" \
    --desktop-file "$appdir/wotm.desktop" --icon-file "$appdir/wotm.svg" \
    --custom-apprun "$repo/tools/release/linux/AppRun" "${excludes[@]}" > "$work/linuxdeploy.log" 2>&1 || {
        tail -100 "$work/linuxdeploy.log" >&2
        fail "linuxdeploy failed"
    }
desktop-file-validate "$appdir/usr/share/applications/wotm.desktop"
python3 "$repo/tools/check_appimage.py" "$appdir"

env -u LD_LIBRARY_PATH ldd -v "$appdir/usr/bin/wotm" > "$work/ldd.txt" 2>&1
if grep -Eq 'not found|version .* not found' "$work/ldd.txt"; then cat "$work/ldd.txt" >&2; fail "unresolved AppDir dependencies or symbol versions"; fi
for soname in libavcodec.so.61 libavutil.so.59 libswscale.so.8; do
    resolved=$(awk -v name="$soname" '$1 == name { print $3; exit }' "$work/ldd.txt")
    [[ "$resolved" == "$appdir/"* ]] || fail "$soname resolved outside AppDir: $resolved"
done
for elf in "$appdir/usr/bin/wotm" "$appdir/usr/lib/"libavcodec.so.61 "$appdir/usr/lib/"libavutil.so.59 "$appdir/usr/lib/"libswscale.so.8; do
    dynamic=$(readelf -d "$elf")
    if grep -Eq '(/home/|/tmp/|/work/|/src/)' <<<"$dynamic"; then fail "build-host path leaked into $elf"; fi
done

env -u LD_LIBRARY_PATH APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 "$appimagetool" --runtime-file "$runtime" "$appdir" "$output" > "$work/appimagetool.log" 2>&1 || {
    tail -100 "$work/appimagetool.log" >&2
    fail "appimagetool failed"
}
chmod 755 "$output"
extract="$work/extracted"
mkdir -p "$extract"
if ! (cd "$extract" && APPIMAGE_EXTRACT_AND_RUN=1 "$output" --appimage-extract > "$work/extract.log" 2>&1) || [[ ! -d "$extract/squashfs-root" ]]; then
    offset=$("$output" --appimage-offset)
    unsquashfs -no-progress -offset "$offset" -d "$extract" "$output" > "$work/unsquashfs.log" 2>&1
    extracted="$extract"
else
    extracted="$extract/squashfs-root"
fi
python3 "$repo/tools/check_appimage.py" "$extracted"

install -m 644 "$archive" "$source_out"
install -m 644 "$signature" "$source_out.asc"
(
    cd "$output_dir"
    sha256sum "${output##*/}" > "${output##*/}.sha256"
    sha256sum "${source_out##*/}" > "${source_out##*/}.sha256"
)
printf 'Built AppImage: %s\nFFmpeg source: %s\n' "$output" "$source_out"
