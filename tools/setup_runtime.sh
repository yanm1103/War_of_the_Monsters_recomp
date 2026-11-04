#!/usr/bin/env bash
# Build the local PS2Recomp tools and translate the game ELF from the user's ISO.
set -euo pipefail

repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
rt="$repo/runtime"
iso=${1:-}
sha_expected=1ed7f8457436a9170d8f85386e267369c7fcdd4f
recomp_commit=2c5fbb9389e11dd95693385969490c9e8e6f57b4

if [[ -z "$iso" ]]; then
    shopt -s nullglob
    candidates=("$repo"/ISO/*.iso "$repo"/Game/*.iso)
    (( ${#candidates[@]} == 1 )) || { echo "Pass one ISO path, or put exactly one .iso in ISO/ or Game/." >&2; exit 2; }
    iso=${candidates[0]}
fi
iso=$(realpath -- "$iso")
[[ -f "$iso" ]] || { echo "ISO not found: $iso" >&2; exit 2; }
for tool in git cmake ninja 7z sha1sum; do command -v "$tool" >/dev/null || { echo "Missing prerequisite: $tool" >&2; exit 2; }; done

mkdir -p "$repo/disc" "$repo/recomp/work" "$repo/recomp/retail" "$rt"
if [[ ! -d "$rt/PS2Recomp/.git" ]]; then
    git clone https://github.com/ran-j/PS2Recomp.git "$rt/PS2Recomp"
    git -C "$rt/PS2Recomp" checkout --detach "$recomp_commit"
fi
[[ $(git -C "$rt/PS2Recomp" rev-parse HEAD) == "$recomp_commit" ]] || {
    echo "runtime/PS2Recomp is not at the expected base commit; refusing to alter it." >&2; exit 2;
}
for patch in wotm-runtime.patch ps2recomp-fpu-sqrt.patch; do
    p="$repo/tools/runtime_patch/$patch"
    if git -C "$rt/PS2Recomp" apply --reverse --check "$p" >/dev/null 2>&1; then
        continue
    fi
    git -C "$rt/PS2Recomp" apply --check --whitespace=nowarn "$p" || {
        echo "Patch does not apply cleanly: $patch" >&2; exit 1;
    }
    git -C "$rt/PS2Recomp" apply --whitespace=nowarn "$p"
done

if [[ ! -x "$rt/build_tools/ps2xAnalyzer/ps2_analyzer" || ! -x "$rt/build_tools/ps2xRecomp/ps2_recomp" ]]; then
    cmake -S "$rt/PS2Recomp" -B "$rt/build_tools" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DPS2X_BUILD_RUNTIME=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF
    cmake --build "$rt/build_tools" --target ps2_analyzer ps2_recomp
fi

# Extract into a fresh directory so an invalid ISO cannot reuse an older disc/SCUS_971.97.
extract_dir=$(mktemp -d "$repo/disc/.iso-extract.XXXXXX")
trap 'rm -rf -- "$extract_dir"' EXIT
7z x -y "-o$extract_dir" "$iso" >/dev/null
elf="$extract_dir/SCUS_971.97"
[[ -f "$elf" ]] || { echo "SCUS_971.97 not found after extracting the ISO." >&2; exit 1; }
sha=$(sha1sum "$elf" | cut -d' ' -f1)
[[ "$sha" == "$sha_expected" ]] || { echo "Unsupported ELF SHA1: $sha (expected $sha_expected)." >&2; exit 1; }
cp -- "$elf" "$repo/disc/SCUS_971.97"
# Preserve existing translated code, including local edits, just like the Windows setup.
if [[ ! -f "$repo/recomp/retail/register_functions.cpp" ]]; then
    cp "$elf" "$repo/recomp/work/game.elf"
    cd "$repo/recomp/work"
    "$rt/build_tools/ps2xAnalyzer/ps2_analyzer" game.elf config.toml
    sed -i 's|^output = .*|output = "../retail/"|' config.toml
    "$rt/build_tools/ps2xRecomp/ps2_recomp" config.toml
    [[ -f "$repo/recomp/retail/register_functions.cpp" ]] || { echo "PS2Recomp did not generate register_functions.cpp." >&2; exit 1; }
fi
echo "Generated local game code in $repo/recomp/retail (not for redistribution)."
echo "Next: tools/build_runtime.sh"
