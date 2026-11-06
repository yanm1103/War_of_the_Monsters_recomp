#!/usr/bin/env bash
# Launch the Linux game using per-user writable data, keeping the ISO and repo inputs local.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
[[ $(uname -m) == x86_64 ]] || { echo "The Linux build currently requires x86_64." >&2; exit 2; }
flags=$(awk '/^flags/{print; exit}' /proc/cpuinfo 2>/dev/null || true)
has_flag() { [[ " $flags " == *" $1 "* ]]; }
for flag in avx2 bmi2 popcnt; do
    has_flag "$flag" || { echo "The CPU must support $flag." >&2; exit 2; }
done
has_flag lzcnt || has_flag abm || { echo "The CPU must support LZCNT (lzcnt/abm)." >&2; exit 2; }
exe="$repo/runtime/build_dev/ps2xRuntime/wotm"
elf="$repo/disc/SCUS_971.97"
iso=${PS2X_CD_IMAGE:-}
if [[ -z "$iso" ]]; then
    shopt -s nullglob
    images=("$repo"/ISO/*.iso "$repo"/Game/*.iso)
    (( ${#images[@]} == 1 )) || { echo "Set PS2X_CD_IMAGE or put exactly one ISO in ISO/ or Game/." >&2; exit 2; }
    iso=${images[0]}
fi
[[ -x "$exe" ]] || { echo "Missing Linux executable: $exe (run tools/build_runtime.sh)." >&2; exit 2; }
[[ -f "$elf" ]] || { echo "Missing extracted ELF: $elf (run tools/setup_runtime.sh)." >&2; exit 2; }
[[ -f "$iso" ]] || { echo "ISO not found: $iso" >&2; exit 2; }
data_home=${XDG_DATA_HOME:-}
if [[ -z "$data_home" || "$data_home" != /* ]]; then
    : "${HOME:?HOME must be set when XDG_DATA_HOME is unavailable or relative.}"
    data_home="$HOME/.local/share"
fi
data="$data_home/WotM"
mkdir -p "$data/mc0" "$data/screenshots"
data_elf="$data/SCUS_971.97"
if [[ ! -e "$data_elf" ]]; then
    cp -- "$elf" "$data_elf"
elif ! cmp -s -- "$elf" "$data_elf"; then
    echo "Existing data ELF differs from the generated retail ELF: $data_elf (back it up and remove it to refresh)." >&2
    exit 2
fi
export PS2X_CD_IMAGE=$(realpath -- "$iso")
export WOTM_REPO="$repo"
assets=${WOTM_ASSET_DIR:-"$repo/native/assets"}
[[ -f "$assets/Oswald-Medium.ttf" && -f "$assets/prompts.png" ]] || { echo "Missing Linux runtime assets in $assets." >&2; exit 2; }
export WOTM_ASSET_DIR=$(realpath -- "$assets")
export WOTM_MODS=${WOTM_MODS:-"$repo/mods"}
: "${PS2X_NO_VU1:=1}"; : "${PS2X_NO_VIF1:=1}"; : "${PS2X_NO_GS:=1}"
: "${PS2X_SCENE:=1}"; : "${PS2X_FMV:=1}"
export PS2X_NO_VU1 PS2X_NO_VIF1 PS2X_NO_GS PS2X_SCENE PS2X_FMV
cd "$data"
"$exe" "$data/SCUS_971.97" 2> >(tee -a live.err.log >&2) | tee -a live.out.log
